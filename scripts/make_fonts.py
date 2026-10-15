#!/usr/bin/env python3
"""Regenerates the text fonts in src/ui/fonts.

Montserrat (from the LVGL library) covers Latin, Cyrillic and Vietnamese. Glyphs it lacks fall
through to Noto Sans for Greek, then to one shared 18px Noto Sans CJK JP for common Chinese
(simplified and traditional), Japanese and Korean. The CJK font is split into three chained files
to keep each generated source under 5MB.

Needs npx (Node) and a prior `pio run` so the LVGL library is in .pio/libdeps. The Noto fonts are
downloaded once into .pio/fonts. Run from the repo root: python3 scripts/make_fonts.py
"""

import pathlib
import re
import subprocess
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "src/ui/fonts"
CACHE = ROOT / ".pio/fonts"
LVGL_FONTS = ROOT / ".pio/libdeps/esp32-s3-knob/lvgl/scripts/built_in_font"
CONV = ["npx", "-y", "lv_font_conv@1.5.2"]

NOTO_URLS = {
    "NotoSans-Medium.ttf": "https://github.com/notofonts/notofonts.github.io/raw/main/fonts/NotoSans/hinted/ttf/NotoSans-Medium.ttf",
    "NotoSansCJKjp-Medium.otf": "https://github.com/notofonts/noto-cjk/raw/main/Sans/OTF/Japanese/NotoSansCJKjp-Medium.otf",
}

LATIN_RANGES = "0x20-0x7F,0xA0-0x17F,0x1A0-0x1A1,0x1AF-0x1B0,0x400-0x4FF,0x1EA0-0x1EF9,0x2022"
GREEK_RANGES = "0x370-0x3FF"
# The FontAwesome symbols LVGL's stock Montserrat carries, so LV_SYMBOL_* still draw in 16 and 20.
SYMBOLS = ("61441,61448,61451,61452,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,"
           "61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,"
           "61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,"
           "61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,"
           "62212,62189,62810,63426,63650")
CJK_SIZE = 18
CJK_RANGES = "0x3000-0x30FF,0x3130-0x318F,0xFF00-0xFFEF"  # punctuation, kana, Hangul jamo, full-width


def double_byte(encoding, rows, cols):
    chars = set()
    for r in rows:
        for c in cols:
            try:
                chars.add(bytes([r, c]).decode(encoding))
            except UnicodeDecodeError:
                pass
    return chars


def cjk_chars():
    """The level-1 sets of the national standards: what nearly all real text uses."""
    gb2312 = double_byte("gb2312", range(0xB0, 0xD8), range(0xA1, 0xFF))  # 3,755 simplified
    jis = {bytes.fromhex(f"{code:04x}").decode("shift_jis", "ignore") for code in range(0x889F, 0x9873)}
    big5 = {bytes.fromhex(f"{code:04x}").decode("big5", "ignore") for code in range(0xA440, 0xC67F)}
    hangul = double_byte("euc_kr", range(0xB0, 0xC9), range(0xA1, 0xFF))  # 2,350 syllables
    han = sorted(c for c in gb2312 | jis | big5 if len(c) == 1 and 0x4E00 <= ord(c) <= 0x9FFF)
    half = len(han) // 2
    return ["".join(han[:half]), "".join(han[half:]), "".join(sorted(hangul))]


def fetch(name):
    path = CACHE / name
    if not path.exists():
        CACHE.mkdir(parents=True, exist_ok=True)
        print(f"downloading {name}")
        urllib.request.urlretrieve(NOTO_URLS[name], path)
    return path


def convert(name, size, font_args, compress, fallback=None):
    out = OUT / f"{name}.c"
    args = CONV + ["--bpp", "4", "--size", str(size), "--format", "lvgl", "--force-fast-kern-format",
                   "--no-prefilter", "-o", str(out)] + font_args
    if not compress:
        args.insert(len(CONV), "--no-compress")
    subprocess.run(args, check=True, stdout=subprocess.DEVNULL)

    src = out.read_text()
    for path in (LVGL_FONTS, CACHE, OUT):
        src = src.replace(str(path) + "/", "")
    src = re.sub(r'#ifdef LV_LVGL_H_INCLUDE_SIMPLE\n#include "lvgl.h"\n#else\n#include "lvgl/lvgl.h"\n#endif',
                 '#include "lvgl.h"', src)
    if fallback:
        src = src.replace('#include "lvgl.h"', f'#include "lvgl.h"\n\nLV_FONT_DECLARE({fallback});', 1)
        src = src.replace("    .dsc = &font_dsc", f"    .fallback = &{fallback},\n    .dsc = &font_dsc", 1)
    src = re.sub(r"--symbols \S+", "--symbols <see scripts/make_fonts.py>", src, count=1)
    out.write_text(src)
    print(f"{out.relative_to(ROOT)}: {out.stat().st_size // 1024} KB of source")


def main():
    montserrat = str(LVGL_FONTS / "Montserrat-Medium.ttf")
    awesome = str(LVGL_FONTS / "FontAwesome5-Solid+Brands+Regular.woff")
    noto = str(fetch("NotoSans-Medium.ttf"))
    noto_cjk = str(fetch("NotoSansCJKjp-Medium.otf"))

    parts = cjk_chars()
    names = [f"lv_font_noto_cjk_{CJK_SIZE}_{i + 1}" for i in range(len(parts))]
    for i, (name, chars) in enumerate(zip(names, parts)):
        font_args = ["--font", noto_cjk, "--symbols", chars]
        if i == len(parts) - 1:
            font_args += ["-r", CJK_RANGES]
        convert(name, CJK_SIZE, font_args, compress=True, fallback=names[i + 1] if i + 1 < len(names) else None)
    cjk = names[0]
    for size in (16, 18, 20):
        greek = f"lv_font_noto_greek_{size}"
        convert(greek, size, ["--font", noto, "-r", GREEK_RANGES], compress=False, fallback=cjk)
        latin = ["--font", montserrat, "-r", LATIN_RANGES]
        if size != 18:
            latin += ["--font", awesome, "-r", SYMBOLS]
        convert(f"lv_font_montserrat_{size}_latin", size, latin, compress=False, fallback=greek)


if __name__ == "__main__":
    main()
