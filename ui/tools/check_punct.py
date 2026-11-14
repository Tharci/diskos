#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail on typographic punctuation the device fonts cannot show: en/em dash, curly quotes, ellipsis.

Usage: check_punct.py FILE...   (run by pre-commit on the UI sources)
"""

import re
import sys
from pathlib import Path

# U+2013/2014 en/em dash, U+2018/2019 and U+201C/201D curly quotes, U+2026 ellipsis.
UNSUPPORTED = {chr(code) for code in (0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026)}
# Generated font data names each glyph in a comment, these included: that is not UI text.
NOT_CHECKED = re.compile(r"^font_.*\.c$")


def main(paths):
    failed = False
    for path in map(Path, paths):
        if NOT_CHECKED.match(path.name):
            continue
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        for number, line in enumerate(lines, 1):
            if UNSUPPORTED.intersection(line):
                print(f"{path}:{number}:{line}")
                failed = True
    if failed:
        print("checkpunct: FAIL (unsupported punctuation above)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
