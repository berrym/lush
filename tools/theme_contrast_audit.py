#!/usr/bin/env python3
"""
WCAG contrast audit for lush builtin prompt themes.

Parses src/lle/prompt/theme.c for `theme->colors.<name> = lle_color_256(N)`
assignments grouped by builtin-theme function, converts each xterm-256 index
to sRGB, and computes WCAG 2.x relative-luminance contrast ratios against
pure black (#000000) and pure white (#FFFFFF). Anything under 4.5:1 (AA for
body text) on the user's likely background fails -- a "primary brand"
segment invisible on most terminals is exactly the failure mode the
corporate theme exhibited on Ghostty (color 24, fixed in 5362a0dd).

Fast read of the output: any cell flagged with ! is below 4.5:1 -- if that
column corresponds to a background the theme targets, the color needs
brightening (for dark bg) or darkening (for light bg).
"""

from __future__ import annotations
import argparse
import math
import re
import sys
from pathlib import Path


# ---- xterm-256 -> sRGB ----------------------------------------------------
# Indices 0-15 (basic 16) are terminal-defined and we don't use them in
# lush themes anyway. 16-231 = 6x6x6 RGB cube. 232-255 = 24-step grayscale.
CUBE_STEPS = (0, 95, 135, 175, 215, 255)


def xterm256_to_rgb(idx: int) -> tuple[int, int, int]:
    if idx < 0 or idx > 255:
        raise ValueError(f"out of range: {idx}")
    if idx < 16:
        # Basic 16 are terminal-defined. We don't expect these in themes;
        # if any slip through, treat as undefined and use approximate
        # xterm defaults.
        approx = {
            0: (0, 0, 0),
            1: (205, 0, 0),
            2: (0, 205, 0),
            3: (205, 205, 0),
            4: (0, 0, 238),
            5: (205, 0, 205),
            6: (0, 205, 205),
            7: (229, 229, 229),
            8: (127, 127, 127),
            9: (255, 0, 0),
            10: (0, 255, 0),
            11: (255, 255, 0),
            12: (92, 92, 255),
            13: (255, 0, 255),
            14: (0, 255, 255),
            15: (255, 255, 255),
        }
        return approx[idx]
    if idx < 232:
        i = idx - 16
        r = (i // 36) % 6
        g = (i // 6) % 6
        b = i % 6
        return CUBE_STEPS[r], CUBE_STEPS[g], CUBE_STEPS[b]
    # grayscale
    v = 8 + (idx - 232) * 10
    return v, v, v


# ---- WCAG 2.x relative luminance + contrast ratio -------------------------
def srgb_to_linear(c8: int) -> float:
    cs = c8 / 255.0
    return cs / 12.92 if cs <= 0.03928 else ((cs + 0.055) / 1.055) ** 2.4


def relative_luminance(rgb: tuple[int, int, int]) -> float:
    r, g, b = (srgb_to_linear(c) for c in rgb)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast_ratio(fg: tuple[int, int, int], bg: tuple[int, int, int]) -> float:
    lf, lb = relative_luminance(fg), relative_luminance(bg)
    bright, dark = max(lf, lb), min(lf, lb)
    return (bright + 0.05) / (dark + 0.05)


# ---- theme.c parser -------------------------------------------------------
THEME_FN_RE = re.compile(
    r"^lle_theme_t\s*\*lle_theme_create_(\w+)\s*\("
    r"|"
    r'\blle_theme_create\(\s*"(\w+)"',
    re.MULTILINE,
)
COLOR_LINE_RE = re.compile(
    r"theme->colors\.(\w+)\s*=\s*lle_color_256\(\s*(\d+)\s*\)"
)
FN_BOUNDARY_RE = re.compile(r"^\}\s*$", re.MULTILINE)


def parse_themes(theme_c: Path) -> dict[str, dict[str, int]]:
    """Return {theme_name: {color_name: 256-index}} for every builtin
    theme found in theme.c. The parser walks function-shaped blocks; a
    theme block starts at lle_theme_create_<name>( and ends at the next
    top-level }. Inside the block, every `theme->colors.X = lle_color_256(N)`
    line records (X, N). If the function calls lle_theme_create("name", ...)
    we use that string as the theme name, otherwise the function-name
    suffix."""
    src = theme_c.read_text()
    themes: dict[str, dict[str, int]] = {}

    fn_re = re.compile(
        r"^lle_theme_t\s*\*lle_theme_create_(\w+)\s*\([^)]*\)\s*\{",
        re.MULTILINE,
    )
    for m in fn_re.finditer(src):
        fn_name_suffix = m.group(1)
        # find matching close brace via brace counting
        depth = 0
        i = m.end() - 1
        start = i
        while i < len(src):
            c = src[i]
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    end = i
                    break
            i += 1
        else:
            continue

        body = src[start : end + 1]

        # Pull the public theme name from the lle_theme_create("..." ...)
        # call inside the body, falling back to the function-name suffix.
        name_match = re.search(r'lle_theme_create\(\s*"([^"]+)"', body)
        public_name = name_match.group(1) if name_match else fn_name_suffix

        colors: dict[str, int] = {}
        for line_match in COLOR_LINE_RE.finditer(body):
            cname, idx = line_match.group(1), int(line_match.group(2))
            colors[cname] = idx

        if colors:
            themes[public_name] = colors

    return themes


# ---- audit + report -------------------------------------------------------
AA_RATIO = 4.5  # WCAG AA for body text
AA_LARGE = 3.0  # WCAG AA for large text / non-text UI
BLACK = (0, 0, 0)
WHITE = (255, 255, 255)


def severity(ratio: float) -> str:
    if ratio >= 7.0:
        return "AAA"
    if ratio >= AA_RATIO:
        return "AA "
    if ratio >= AA_LARGE:
        return "AA-large"
    return "FAIL"


def short_severity(ratio: float) -> str:
    if ratio >= 7.0:
        return "  "
    if ratio >= AA_RATIO:
        return "  "
    if ratio >= AA_LARGE:
        return " ?"
    return " !"


def hex6(rgb: tuple[int, int, int]) -> str:
    return f"#{rgb[0]:02x}{rgb[1]:02x}{rgb[2]:02x}"


def audit(themes: dict[str, dict[str, int]], failures_only: bool) -> int:
    n_fail = 0
    print()
    print("Contrast audit (WCAG 2.x). AA threshold: 4.5:1 for body text.")
    print("Columns: ratio vs pure-black bg, ratio vs pure-white bg.")
    print(f"  '  ' = passes AA on this bg")
    print(f"  ' ?' = AA-large only ({AA_LARGE}:1) -- borderline")
    print(f"  ' !' = below {AA_LARGE}:1 -- fails")
    print()

    # Sort themes by an order that puts dark-targeted ones first then light
    sort_order = ["minimal", "default", "powerline", "informative", "two-line",
                  "nerd", "corporate", "dark", "light", "colorful"]
    key = lambda n: sort_order.index(n) if n in sort_order else 99

    for theme_name in sorted(themes, key=key):
        colors = themes[theme_name]
        # Build per-row entries first so we can compute a per-theme summary.
        rows = []
        for cname, idx in colors.items():
            rgb = xterm256_to_rgb(idx)
            ratio_black = contrast_ratio(rgb, BLACK)
            ratio_white = contrast_ratio(rgb, WHITE)
            black_fail = ratio_black < AA_RATIO
            white_fail = ratio_white < AA_RATIO
            rows.append((cname, idx, rgb, ratio_black, ratio_white,
                         black_fail, white_fail))
            if black_fail and white_fail:
                # Color fails on BOTH backgrounds -- this is the hard case
                n_fail += 1
            elif black_fail or white_fail:
                # Fails on one background; OK if theme only targets the other
                pass

        if failures_only:
            # Suppress themes with no problems
            problem = any(b or w for _, _, _, _, _, b, w in rows)
            if not problem:
                continue

        print(f"=== theme: {theme_name} ===")
        print(f"{'color name':22s}  idx  hex      vs black     vs white")
        print(f"{'-'*22}  ---  -------  --------     --------")
        for cname, idx, rgb, rb, rw, bf, wf in rows:
            mark_b = short_severity(rb)
            mark_w = short_severity(rw)
            print(f"{cname:22s}  {idx:3d}  {hex6(rgb):7s}"
                  f"  {rb:5.2f}:1{mark_b}   {rw:5.2f}:1{mark_w}")
        print()

    return n_fail


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--theme-c",
                   default="src/lle/prompt/theme.c",
                   help="path to theme.c (default: src/lle/prompt/theme.c)")
    p.add_argument("--failures-only", action="store_true",
                   help="suppress themes whose colors all pass AA")
    args = p.parse_args(argv)

    theme_c = Path(args.theme_c)
    if not theme_c.exists():
        print(f"error: {theme_c} not found", file=sys.stderr)
        return 1

    themes = parse_themes(theme_c)
    if not themes:
        print(f"warning: no themes parsed from {theme_c}", file=sys.stderr)
        return 1

    n_bidir_fail = audit(themes, args.failures_only)
    print(f"summary: {len(themes)} themes audited, "
          f"{n_bidir_fail} color(s) fail AA on BOTH black and white "
          "(invisible no matter what background the user picks)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
