#!/usr/bin/env python3
"""Generate src/sdl2_cursor_bitmaps.h from the X.Org cursor font.

The original xboing editor used X11 cursor-font glyphs: XC_plus (90) for
the draw cursor and XC_pirate (88, skull-and-crossbones) for the erase
cursor (original/init.c:849,857).  SDL2 has no equivalent system cursors,
so we reproduce the exact 1996 bitmaps.

X's cursor font stores each cursor as a monochrome *source* glyph (even
code) plus a *mask* glyph (code+1), positioned relative to a shared origin
(the hotspot) via their BBX offsets.  That maps directly onto the classic
SDL_CreateCursor(data, mask, w, h, hot_x, hot_y).  This tool extracts the
source+mask, aligns them on a common byte-padded canvas, and emits C data.

Source font: X.Org 'cursor' bitmap font (font-cursor-misc, cursor.bdf),
MIT/X11 licensed.  A convenient mirror:
  https://raw.githubusercontent.com/olikraus/u8g2/master/tools/font/bdf/cursor.bdf

Usage:
  python3 scripts/gen_cursors.py path/to/cursor.bdf > src/sdl2_cursor_bitmaps.h
Pass --ascii to also print a preview of each cursor to stderr.
"""
import sys


def parse_bdf(path):
    glyphs = {}
    enc = bbx = rows = None
    inbmp = False
    with open(path) as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("ENCODING"):
                enc = int(line.split()[1])
            elif line.startswith("BBX"):
                bbx = list(map(int, line.split()[1:]))  # w h xoff yoff
            elif line == "BITMAP":
                inbmp, rows = True, []
            elif line.startswith("ENDCHAR"):
                glyphs[enc] = (bbx, rows)
                inbmp = False
            elif inbmp:
                rows.append(int(line, 16))
    return glyphs


def lit_pixels(glyphs, enc):
    """Set of (x, y) lit pixels in origin coords (x right, y down), origin
    (0,0) = hotspot.  Row hex is MSB-left over ceil(w/8)*8 bits."""
    (w, h, xoff, yoff), rows = glyphs[enc]
    rowbits = ((w + 7) // 8) * 8
    out = set()
    for ry, val in enumerate(rows):
        for cx in range(w):
            if (val >> (rowbits - 1 - cx)) & 1:
                gy_math = yoff + (h - 1 - ry)  # BDF y is up from origin
                out.add((xoff + cx, -gy_math))
    return out


def build(glyphs, src_enc, mask_enc, name, ascii_preview):
    src = lit_pixels(glyphs, src_enc)
    msk = lit_pixels(glyphs, mask_enc)
    allp = src | msk
    minx, maxx = min(x for x, _ in allp), max(x for x, _ in allp)
    miny, maxy = min(y for _, y in allp), max(y for _, y in allp)
    width, height = maxx - minx + 1, maxy - miny + 1
    row_bytes = (maxx - minx) // 8 + 1
    hotx, hoty = -minx, -miny
    data = [[0] * row_bytes for _ in range(height)]
    mask = [[0] * row_bytes for _ in range(height)]
    for (x, y) in msk:
        cx, cy = x - minx, y - miny
        mask[cy][cx // 8] |= 0x80 >> (cx % 8)
    for (x, y) in src:
        cx, cy = x - minx, y - miny
        data[cy][cx // 8] |= 0x80 >> (cx % 8)
    if ascii_preview:
        print(f"; {name}: {width}x{height} hot=({hotx},{hoty})", file=sys.stderr)
        for y in range(height):
            line = ""
            for x in range(width):
                m = (mask[y][x // 8] >> (7 - x % 8)) & 1
                d = (data[y][x // 8] >> (7 - x % 8)) & 1
                line += "#" if (m and d) else ("." if m else " ")
            print("  " + line, file=sys.stderr)
    return name, width, height, hotx, hoty, data, mask


def emit_c(defs):
    print("/*")
    print(" * sdl2_cursor_bitmaps.h -- GENERATED, do not edit by hand.")
    print(" *")
    print(" * Exact monochrome source+mask bitmaps for the original editor")
    print(" * cursors, from the X11 cursor font: XC_plus (90, draw) and")
    print(" * XC_pirate (88, skull-and-crossbones, erase) -- original")
    print(" * init.c:849,857 -- aligned to a common canvas + hotspot for")
    print(" * SDL_CreateCursor.")
    print(" *")
    print(" * Source: X.Org 'cursor' bitmap font (font-cursor-misc), MIT/X11.")
    print(" * Regenerate: python3 scripts/gen_cursors.py cursor.bdf > this file.")
    print(" */")
    print("#ifndef SDL2_CURSOR_BITMAPS_H")
    print("#define SDL2_CURSOR_BITMAPS_H")
    print()
    for (name, width, height, hx, hy, data, mask) in defs:
        for arr, suffix in ((data, "data"), (mask, "mask")):
            print(f"static const unsigned char {name}_{suffix}[] = {{")
            for row in arr:
                print("    " + ", ".join(f"0x{b:02x}" for b in row) + ",")
            print("};")
        print(f"#define {name.upper()}_W {width}")
        print(f"#define {name.upper()}_H {height}")
        print(f"#define {name.upper()}_HOTX {hx}")
        print(f"#define {name.upper()}_HOTY {hy}")
        print()
    print("#endif /* SDL2_CURSOR_BITMAPS_H */")


def main():
    argv = sys.argv[1:]
    ascii_preview = "--ascii" in argv
    args = [a for a in argv if a != "--ascii"]
    if not args:
        sys.exit("usage: gen_cursors.py path/to/cursor.bdf [--ascii] > out.h")
    glyphs = parse_bdf(args[0])
    missing = [e for e in (88, 89, 90, 91) if e not in glyphs]
    if missing:
        sys.exit(f"error: {args[0]} is missing cursor glyph(s) {missing} "
                 "(expected XC_plus 90/91, XC_pirate 88/89)")
    emit_c([
        build(glyphs, 90, 91, "cursor_plus", ascii_preview),
        build(glyphs, 88, 89, "cursor_pirate", ascii_preview),
    ])


if __name__ == "__main__":
    main()
