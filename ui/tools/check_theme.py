#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail if UI code builds a colour or picks a font directly instead of through a theme role.

Every on-screen colour is a theme role (theme.h); theme.c is the only place RGB becomes a
colour. This fails if any other source or header builds a colour itself: an LVGL colour
constructor, palette, mix/lighten/darken or HSV conversion, a colour macro or struct
initialiser, a literal passed to the runtime-colour helpers, or a literal B/G/R byte written
into a generated image buffer. Fonts: no direct Montserrat/icon font (by address or copy;
LV_FONT_DECLARE lines are fine).

Known limits: a colour laundered through a macro, a decimal or computed-index pixel write, or
an unrelated struct field named red/green/blue are not told apart.

Usage: check_theme.py FILE...
(CMake passes the app sources and headers before each link; pre-commit the staged files.)
"""

import re
import sys
from pathlib import Path

SP = r"\s"
COLOUR = re.compile(
    "|".join(
        (
            rf"lv_color(32|16)?_(hex|hex3|make|white|black|mix|mix32|16_16_mix|lighten|darken"
            rf"|hsv_to_rgb|from_int|to_hsv|premultiply){SP}*\(",
            rf"lv_color(32|16)_t{SP}+[A-Za-z_0-9]+{SP}*={SP}*\{{",
            rf"\(lv_color(32|16)_t\){SP}*\{{",
            rf"lv_palette_[a-z]+{SP}*\(",
            rf"LV_COLOR_MAKE{SP}*\(",
            rf"theme_(color|accent)_from_(rgb|hsv){SP}*\({SP}*(0x|[0-9])",
            rf"\(lv_color_t\){SP}*\{{",
            rf"lv_color_t{SP}+[A-Za-z_0-9]+{SP}*={SP}*\{{",
            rf"(\.|->)(red|green|blue){SP}*=[^=]",
            rf"\[[0-2]\]{SP}*={SP}*0x[0-9A-Fa-f]{{2}}",
        )
    )
)
FONT = re.compile(r"\b(lv_font_montserrat_[0-9]+|font_icons_[0-9]+)\b")

# Files allowed to do what the rules forbid: the theme itself, generated fonts, vendored code.
NOT_CHECKED = re.compile(r"^(theme\.[ch]|font_.*\.c|sqlite3.*|stb_image\.h)$")
NOT_FONT_CHECKED = {"lv_conf.h"}


def findings(path):
    if NOT_CHECKED.match(path.name) or "lvgl" in path.parts:
        return
    text = path.read_text(encoding="utf-8", errors="replace").splitlines()
    for number, line in enumerate(text, 1):
        if COLOUR.search(line):
            yield "colour", f"{path}:{number}:{line}"
        if (
            path.name not in NOT_FONT_CHECKED
            and FONT.search(line)
            and "LV_FONT_DECLARE" not in line
        ):
            yield "font", f"{path}:{number}:{line}"


def main(paths):
    found = {"colour": [], "font": []}
    for path in paths:
        for kind, where in findings(Path(path)):
            found[kind].append(where)
    for where in found["colour"]:
        print(where)
    if found["colour"]:
        print(
            "check-theme: FAIL - use a theme role (TC(...)), or theme_color_from_rgb() on a "
            "runtime value"
        )
    for where in found["font"]:
        print(where)
    if found["font"]:
        print("check-theme: FAIL - use a font role (TF(...)) instead")
    return 1 if found["colour"] or found["font"] else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
