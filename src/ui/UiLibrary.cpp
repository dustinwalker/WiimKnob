#include "UiInternal.h"

LV_FONT_DECLARE(lv_font_star_16);

namespace ui {

using namespace detail;

namespace {

constexpr int kMaxTabs = 4;
constexpr int kMaxRows = 100;
constexpr int kThumbSize = 48;

// A page of tabs over scrolling rows: Library (Roon) and TuneIn.
struct ListPage {
  int screen;
  int tabCount;
  const char *const *tabNames; // indexed by tab
  const int *tabOrder;         // how the tabs sit on screen
  void (*onItem)(int tab, int index);
  void (*onTab)(int tab);
  void (*onHeld)(int tab, int index); // nullptr: holding a row does nothing
  lv_obj_t *tabLabels[kMaxTabs];
  lv_obj_t *lists[kMaxTabs];
  lv_obj_t *thumbs[kMaxTabs][kMaxRows];
  int rowCounts[kMaxTabs];
  char emptyText[kMaxTabs][64];
  lv_obj_t *emptyLabel;
  int currentTab;
};

const char *const kLibraryTabs[] = {"History", "Playlists", "Radio", "Up next"};
const char *const kLibraryEmpty[] = {"Tracks you play from Roon will show up here", "No playlists in Roon yet",
                                     "No stations in My Live Radio", "Nothing queued in Roon"};
const int kLibraryOrder[] = {0, 3, 1, 2};
const char *const kTuneInTabs[] = {"Favorites", "Local", "Browse"};
const int kTuneInOrder[] = {0, 1, 2};

void libraryItem(int tab, int index) {
  if (callbacks.onLibraryItem) callbacks.onLibraryItem(tab, index);
}
void libraryTab(int tab) {
  if (callbacks.onLibraryTab) callbacks.onLibraryTab(tab);
}
void tuneInItem(int tab, int index) {
  if (callbacks.onTuneInItem) callbacks.onTuneInItem(tab, index);
}
void tuneInTab(int tab) {
  if (callbacks.onTuneInTab) callbacks.onTuneInTab(tab);
}
void tuneInHeld(int tab, int index) {
  if (callbacks.onTuneInHeld) callbacks.onTuneInHeld(tab, index);
}

ListPage library = {kLibrary, 4, kLibraryTabs, kLibraryOrder, libraryItem, libraryTab, nullptr};
ListPage tuneIn = {kTuneIn, 3, kTuneInTabs, kTuneInOrder, tuneInItem, tuneInTab, tuneInHeld};

// LVGL still sends CLICKED at the end of a page swipe that started on a row.
bool wasSwipe() {
  lv_indev_t *indev = lv_indev_get_act();
  return indev && lv_indev_get_gesture_dir(indev) != LV_DIR_NONE;
}

void showTab(ListPage &page, int tab) {
  page.currentTab = tab;
  for (int i = 0; i < page.tabCount; i++) {
    lv_obj_set_style_text_color(page.tabLabels[i], lv_color_hex(i == tab ? 0xffffff : 0x707070), 0);
    if (i == tab) lv_obj_clear_flag(page.lists[i], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(page.lists[i], LV_OBJ_FLAG_HIDDEN);
  }
  lv_label_set_text(page.emptyLabel, page.emptyText[tab]);
  if (page.rowCounts[tab]) lv_obj_add_flag(page.emptyLabel, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(page.emptyLabel, LV_OBJ_FLAG_HIDDEN);
}

// Tab and row events carry page * 100000 + tab * 1000 + index.
ListPage &pageFor(int packed) { return packed / 100000 == kTuneIn ? tuneIn : library; }

void onTabClicked(lv_event_t *e) {
  if (wasSwipe()) return;
  int packed = (int)(intptr_t)lv_event_get_user_data(e);
  ListPage &page = pageFor(packed);
  int tab = packed % 100000 / 1000;
  showTab(page, tab);
  page.onTab(tab);
}

void onRowClicked(lv_event_t *e) {
  if (wasSwipe()) return;
  int packed = (int)(intptr_t)lv_event_get_user_data(e);
  pageFor(packed).onItem(packed % 100000 / 1000, packed % 1000);
}

void onRowHeld(lv_event_t *e) {
  int packed = (int)(intptr_t)lv_event_get_user_data(e);
  ListPage &page = pageFor(packed);
  if (page.onHeld) page.onHeld(packed % 100000 / 1000, packed % 1000);
}

lv_obj_t *makeLabel(lv_obj_t *row, const char *text, uint32_t color, lv_align_t align, int y) {
  lv_obj_t *label = lv_label_create(row);
  lv_obj_set_size(label, 192, lv_font_get_line_height(&lv_font_montserrat_16_latin)); // DOT only truncates at a fixed height
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16_latin, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_label_set_text(label, text);
  lv_obj_align(label, align, kThumbSize + 10, y);
  return label;
}

void buildPage(ListPage &page) {
  lv_obj_t *scr = screens[page.screen];

  lv_obj_t *tabRow = lv_obj_create(scr);
  lv_obj_set_size(tabRow, 250, 28);
  lv_obj_align(tabRow, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_set_style_bg_opa(tabRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(tabRow, 0, 0);
  lv_obj_set_style_pad_all(tabRow, 0, 0);
  lv_obj_set_style_pad_column(tabRow, 14, 0);
  lv_obj_set_flex_flow(tabRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(tabRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(tabRow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(tabRow, LV_OBJ_FLAG_GESTURE_BUBBLE);
  for (int slot = 0; slot < page.tabCount; slot++) {
    int i = page.tabOrder[slot];
    lv_obj_t *tab = lv_label_create(tabRow);
    lv_obj_set_style_text_font(tab, &lv_font_montserrat_14, 0);
    lv_label_set_text(tab, page.tabNames[i]);
    lv_obj_add_flag(tab, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(tab, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(tab, 12);
    lv_obj_add_event_cb(tab, onTabClicked, LV_EVENT_CLICKED, (void *)(intptr_t)(page.screen * 100000 + i * 1000));
    page.tabLabels[i] = tab;
  }
  for (int i = 0; i < page.tabCount; i++) {
    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_set_size(list, 276, 228);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 76);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_style_pad_bottom(list, 40, 0); // lets the last row scroll up clear of the curved edge
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(list, LV_OBJ_FLAG_GESTURE_BUBBLE);
    page.lists[i] = list;
  }

  page.emptyLabel = lv_label_create(scr);
  lv_obj_set_width(page.emptyLabel, 220);
  lv_label_set_long_mode(page.emptyLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(page.emptyLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(page.emptyLabel, &lv_font_montserrat_16_latin, 0);
  lv_obj_set_style_text_color(page.emptyLabel, lv_color_hex(0xa0a0a0), 0);
  lv_obj_center(page.emptyLabel);

  makePageDots(scr, page.screen);
  showTab(page, 0);
}

void setRows(ListPage &page, int tab, const LibraryRow *rows, int count) {
  if (tab < 0 || tab >= page.tabCount) return;
  count = LV_MIN(count, kMaxRows);
  lv_obj_t *list = page.lists[tab];
  lv_coord_t scrollY = lv_obj_get_scroll_y(list);
  lv_obj_clean(list);
  for (int i = 0; i < count; i++) {
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_set_size(row, 264, 58);
    lv_obj_set_style_radius(row, 14, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 5, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x1c1c1c), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x3a3a3a), LV_STATE_PRESSED);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
    void *packed = (void *)(intptr_t)(page.screen * 100000 + tab * 1000 + i);
    if (page.onHeld) {
      lv_obj_add_event_cb(row, onRowClicked, LV_EVENT_SHORT_CLICKED, packed); // a hold mustn't also tap
      lv_obj_add_event_cb(row, onRowHeld, LV_EVENT_LONG_PRESSED, packed);
    } else {
      lv_obj_add_event_cb(row, onRowClicked, LV_EVENT_CLICKED, packed);
    }

    lv_obj_t *img = lv_img_create(row);
    lv_obj_set_size(img, kThumbSize, kThumbSize);
    lv_obj_align(img, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(img, lv_color_hex(0x2c2c2c), 0);
    lv_obj_set_style_bg_opa(img, LV_OPA_COVER, 0);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    if (rows[i].thumb) lv_img_set_src(img, rows[i].thumb);
    page.thumbs[tab][i] = img;

    lv_obj_t *title = makeLabel(row, rows[i].title, 0xffffff, LV_ALIGN_TOP_LEFT, 2);
    makeLabel(row, rows[i].subtitle, 0x9a9a9a, LV_ALIGN_BOTTOM_LEFT, -2);
    if (rows[i].starred) {
      lv_obj_set_width(title, 172);
      lv_obj_t *star = lv_label_create(row);
      lv_obj_set_style_text_font(star, &lv_font_star_16, 0);
      lv_obj_set_style_text_color(star, lv_color_hex(0xf5c518), 0);
      lv_label_set_text(star, "\xEF\x80\x85"); // U+F005
      lv_obj_align(star, LV_ALIGN_TOP_RIGHT, -4, 3);
    }
  }
  page.rowCounts[tab] = count;
  lv_obj_scroll_to_y(list, scrollY, LV_ANIM_OFF);
  if (tab == page.currentTab) showTab(page, tab);
}

void setThumb(ListPage &page, int tab, int index, const lv_img_dsc_t *thumb) {
  if (tab < 0 || tab >= page.tabCount || index < 0 || index >= page.rowCounts[tab] || !thumb) return;
  lv_img_set_src(page.thumbs[tab][index], thumb);
}

} // namespace

void detail::buildLibraryScreen() {
  for (int i = 0; i < library.tabCount; i++) strlcpy(library.emptyText[i], kLibraryEmpty[i], sizeof(library.emptyText[i]));
  strlcpy(tuneIn.emptyText[kTuneInFavorites], "Hold a station to save it here", sizeof(tuneIn.emptyText[0]));
  buildPage(library);
  buildPage(tuneIn);
}

void setLibraryRows(int tab, const LibraryRow *rows, int count) { setRows(library, tab, rows, count); }
void setLibraryThumb(int tab, int index, const lv_img_dsc_t *thumb) { setThumb(library, tab, index, thumb); }

void setTuneInRows(int tab, const LibraryRow *rows, int count) { setRows(tuneIn, tab, rows, count); }
void setTuneInThumb(int tab, int index, const lv_img_dsc_t *thumb) { setThumb(tuneIn, tab, index, thumb); }

void setTuneInEmptyText(int tab, const char *text) {
  if (tab < 0 || tab >= tuneIn.tabCount || strcmp(tuneIn.emptyText[tab], text) == 0) return;
  strlcpy(tuneIn.emptyText[tab], text, sizeof(tuneIn.emptyText[tab]));
  if (tab == tuneIn.currentTab) showTab(tuneIn, tab);
}

void scrollTuneInToTop(int tab) {
  if (tab >= 0 && tab < tuneIn.tabCount) lv_obj_scroll_to_y(tuneIn.lists[tab], 0, LV_ANIM_OFF);
}

} // namespace ui
