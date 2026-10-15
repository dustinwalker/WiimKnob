#include "UiInternal.h"

namespace ui {

using namespace detail;

namespace {

constexpr int kTabs = 3;
constexpr int kMaxRows = 100;
constexpr int kThumbSize = 48;
const char *const kTabNames[kTabs] = {"History", "Playlists", "Radio"};
const char *const kEmptyText[kTabs] = {"Tracks you play from Roon will show up here",
                                       "No playlists in Roon yet", "No stations in My Live Radio"};

lv_obj_t *tabLabels[kTabs];
lv_obj_t *lists[kTabs];
lv_obj_t *thumbs[kTabs][kMaxRows];
int rowCounts[kTabs];
lv_obj_t *emptyLabel;
int currentTab = 0;

void showTab(int tab) {
  currentTab = tab;
  for (int i = 0; i < kTabs; i++) {
    lv_obj_set_style_text_color(tabLabels[i], lv_color_hex(i == tab ? 0xffffff : 0x707070), 0);
    if (i == tab) lv_obj_clear_flag(lists[i], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(lists[i], LV_OBJ_FLAG_HIDDEN);
  }
  lv_label_set_text(emptyLabel, kEmptyText[tab]);
  if (rowCounts[tab]) lv_obj_add_flag(emptyLabel, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(emptyLabel, LV_OBJ_FLAG_HIDDEN);
}

void onTabClicked(lv_event_t *e) {
  int tab = (int)(intptr_t)lv_event_get_user_data(e);
  showTab(tab);
  if (callbacks.onLibraryTab) callbacks.onLibraryTab(tab);
}

void onRowClicked(lv_event_t *e) {
  int packed = (int)(intptr_t)lv_event_get_user_data(e);
  if (callbacks.onLibraryItem) callbacks.onLibraryItem(packed / 1000, packed % 1000);
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

} // namespace

void detail::buildLibraryScreen() {
  lv_obj_t *scr = screens[kLibrary];

  for (int i = 0; i < kTabs; i++) {
    lv_obj_t *tab = lv_label_create(scr);
    lv_obj_set_style_text_font(tab, &lv_font_montserrat_16, 0);
    lv_label_set_text(tab, kTabNames[i]);
    lv_obj_align(tab, LV_ALIGN_TOP_MID, (i - 1) * 84, 44);
    lv_obj_add_flag(tab, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(tab, 12);
    lv_obj_add_event_cb(tab, onTabClicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    tabLabels[i] = tab;

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
    lists[i] = list;
  }

  emptyLabel = lv_label_create(scr);
  lv_obj_set_width(emptyLabel, 220);
  lv_label_set_long_mode(emptyLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(emptyLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(emptyLabel, &lv_font_montserrat_16_latin, 0);
  lv_obj_set_style_text_color(emptyLabel, lv_color_hex(0xa0a0a0), 0);
  lv_obj_center(emptyLabel);

  makePageDots(scr, kLibrary);
  showTab(0);
}

void setLibraryRows(int tab, const LibraryRow *rows, int count) {
  if (tab < 0 || tab >= kTabs) return;
  count = LV_MIN(count, kMaxRows);
  lv_obj_t *list = lists[tab];
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
    lv_obj_add_event_cb(row, onRowClicked, LV_EVENT_CLICKED, (void *)(intptr_t)(tab * 1000 + i));

    lv_obj_t *img = lv_img_create(row);
    lv_obj_set_size(img, kThumbSize, kThumbSize);
    lv_obj_align(img, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(img, lv_color_hex(0x2c2c2c), 0);
    lv_obj_set_style_bg_opa(img, LV_OPA_COVER, 0);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    if (rows[i].thumb) lv_img_set_src(img, rows[i].thumb);
    thumbs[tab][i] = img;

    makeLabel(row, rows[i].title, 0xffffff, LV_ALIGN_TOP_LEFT, 2);
    makeLabel(row, rows[i].subtitle, 0x9a9a9a, LV_ALIGN_BOTTOM_LEFT, -2);
  }
  rowCounts[tab] = count;
  lv_obj_scroll_to_y(list, scrollY, LV_ANIM_OFF);
  if (tab == currentTab) showTab(tab);
}

void setLibraryThumb(int tab, int index, const lv_img_dsc_t *thumb) {
  if (tab < 0 || tab >= kTabs || index < 0 || index >= rowCounts[tab] || !thumb) return;
  lv_img_set_src(thumbs[tab][index], thumb);
}

} // namespace ui
