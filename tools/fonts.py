#!/usr/bin/env python3
"""Fonts of the game: the glyph images of `Zeus_Fonts.sg3` / `zeus_Fonts.555` and the character
table of the executable.

Format description: formats/fonts.md (the sprite container itself: formats/sg3.md). Standard
library only.

Usage:
    fonts.py list  [GAME]                  the fonts: rank, group name, first image, sizes
    fonts.py check [GAME]                  decode every image of the font file and check the layout
                                           that formats/fonts.md describes; exit 1 on any failure
    fonts.py table [GAME|EXE] [CODEPAGE]   the character table: character -> glyph number
    fonts.py sheet FONT OUT.png [GAME]     every glyph of a font, 10 to a row, numbered
    fonts.py text  FONT TEXT OUT.png [GAME] [CODEPAGE]
                                           a line of text laid out with the game's rules (see
                                           formats/fonts.md: advance, space, raised upper half)

GAME is the game folder (the one with Zeus.exe and DATA), by default original/gog/app. FONT is a
rank (0 to 16, the order the game uses), a group name (SPR_FONTS_11C) or the image number of the
first glyph. CODEPAGE (default cp1250, the GOG build) decodes or encodes the characters. PNG
files are pictures of game files: write them inside the project (build/ for instance), never
commit them.
"""
import os
import struct
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sg3  # noqa: E402

DEFAULT_GAME = os.path.join(sg3.PROJECT_ROOT, "original", "gog", "app")
GLYPHS = 134                  # images per font
CHAR_TABLE_VA = 0x0063BE2C    # 224 bytes, characters 0x20 to 0xFF (docs/engine/interface.md 7.3)
CHAR_TABLE_SIZE = 224
MASK_TYPE = 20                # one-colour glyph masks, coloured by the game (SPR_FONTS)

# The string renderer 0x00508800 (docs/engine/interface.md 7.2): by rank, the space width, the
# line height used to raise the upper half of the table, and the extra pixel after each glyph.
SPACE = {0: 4, 6: 8, 7: 8, 16: 4}
LINE_HEIGHT = {0: 9, 6: 0x17, 7: 0x18, 16: 7}
EXTRA_PIXEL = {0: 1}


def find(folder, name):
    """A file of `folder` whose name matches `name` regardless of case, or None."""
    try:
        return next((os.path.join(folder, n) for n in os.listdir(folder) if n.lower() == name.lower()), None)
    except FileNotFoundError:
        return None


def font_file(game):
    data = find(game, "DATA")
    path = data and find(data, "Zeus_Fonts.sg3")
    if not path:
        sys.exit(f"Zeus_Fonts.sg3 not found under {game}/DATA")
    return sg3.SgFile(path)


def executable(path):
    if os.path.isdir(path):
        exe = find(path, "Zeus.exe")
        if not exe:
            sys.exit(f"Zeus.exe not found in {path}")
        return exe
    return path


def fonts(sg):
    """The fonts in the order the game ranks them: (rank, name, first image). The rank counts the
    non-empty groups of the file in the order of the group names (formats/sg3.md)."""
    named = [(name, sg.groups[i]) for i, name in enumerate(sg.tags) if name and sg.groups[i]]
    return [(rank, name, first) for rank, (name, first) in enumerate(named)]


def blocks(sg):
    """Blocks of GLYPHS images from the first font image to the end of the file, in file order."""
    start = min(first for _, _, first in fonts(sg))
    return list(range(start, sg.num_images + 1, GLYPHS))


def resolve(sg, spec):
    for rank, name, first in fonts(sg):
        if spec in (str(rank), name, str(first)):
            return rank, name, first
    if spec.isdigit() and int(spec) in blocks(sg):
        return None, "(no group name)", int(spec)
    sys.exit(f"unknown font {spec!r}: give a rank, a group name or a first image number")


def char_table(exe_path):
    """The 224 bytes of the character table, read from the file through the section headers."""
    with open(exe_path, "rb") as fh:
        d = fh.read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    sections, optional = struct.unpack_from("<H", d, pe + 6)[0], struct.unpack_from("<H", d, pe + 20)[0]
    base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
    for i in range(sections):
        o = pe + 24 + optional + 40 * i
        size, va, raw_size, raw = struct.unpack_from("<IIII", d, o + 8)
        if base + va <= CHAR_TABLE_VA and CHAR_TABLE_VA + CHAR_TABLE_SIZE <= base + va + raw_size:
            at = raw + CHAR_TABLE_VA - base - va
            return d[at:at + CHAR_TABLE_SIZE], at
    sys.exit(f"{exe_path}: no section holds {CHAR_TABLE_VA:#x}")


def runs_by_row(sg, im):
    """Walks the runs of a compressed image; returns (runs, runs that cross the end of a row,
    longest colour run). Raises sg3.FormatError if the runs do not fill the image exactly."""
    d = sg.data(im)[:im.length]
    i = pos = crossing = longest = runs = 0
    w = im.width
    while i < len(d):
        c = d[i]
        i += 1
        n = d[i] if c == 255 else c
        if c == 255:
            i += 1
        else:
            i += 2 * c
            longest = max(longest, c)
        if n and pos // w != (pos + n - 1) // w:
            crossing += 1
        pos += n
        runs += 1
    if pos != w * im.height or i != len(d):
        raise sg3.FormatError(f"image {im.index}: runs cover {pos} pixels of {w * im.height}")
    return runs, crossing, longest


def cmd_list(game):
    sg = font_file(game)
    print(f"{os.path.basename(sg.path)}: {sg.num_images} images, bitmaps "
          + ", ".join(f"{b.filename} ({b.start}-{b.end})" for b in sg.bitmaps))
    named = {first: (rank, name) for rank, name, first in fonts(sg)}
    for first in blocks(sg):
        rank, name = named.get(first, (None, "(no group name)"))
        ims = sg.images[first:first + GLYPHS]
        heights = Counter(im.height for im in ims)
        kind = "mask" if all(im.type == MASK_TYPE for im in ims) else "colour"
        print(f"  rank {'-' if rank is None else rank:>2}  {name:22} images {first}-{first + GLYPHS - 1}  {kind:6}"
              f"  height {min(heights)}-{max(heights)} (mostly {heights.most_common(1)[0][0]})"
              f"  width {min(im.width for im in ims)}-{max(im.width for im in ims)}")


def cmd_check(game):
    sg = font_file(game)
    failures = []
    decoded = 0
    for im in sg.images[1:]:
        try:
            sg.decode(im)
            decoded += 1
        except sg3.FormatError as e:
            failures.append(str(e))
    print(f"decoded {decoded} of {sg.num_images} images")
    ims = sg.images[1:]
    gaps = [a.index for a, b in zip(ims, ims[1:]) if a.length and b.length and b.offset != a.offset + a.length]
    if gaps:
        failures.append(f"image data not contiguous in record order after images {gaps[:5]}")
    found = fonts(sg)
    if len(found) != 17:
        failures.append(f"{len(found)} named font groups instead of 17")
    starts = blocks(sg)
    if sorted(first for _, _, first in found) != [s for s in starts if s in {f for _, _, f in found}] or \
            starts[-1] + GLYPHS - 1 != sg.num_images:
        failures.append("the fonts are not whole blocks of 134 images up to the end of the file")
    runs = crossing = longest = 0
    anchors = Counter()
    for first in starts:
        for im in sg.images[first:first + GLYPHS]:
            anchors[(im.sprite_offset_x, im.sprite_offset_y)] += 1
            if im.invert_offset or im.external or im.type not in (0, MASK_TYPE):
                failures.append(f"image {im.index}: type {im.type}, mirror {im.invert_offset}, external {im.external}")
            if im.fully_compressed:
                try:
                    r, c, n = runs_by_row(sg, im)
                except sg3.FormatError as e:
                    failures.append(str(e))
                    continue
                runs, crossing, longest = runs + r, crossing + c, max(longest, n)
    print(f"{len(found)} named fonts and {len(starts) - len(found)} unnamed block(s) of {GLYPHS} glyphs, "
          f"images {starts[0]}-{sg.num_images}")
    print(f"glyph runs: {runs}, crossing a row end: {crossing}, longest colour run: {longest}")
    print("glyph anchors (record offsets 34, 36): " + ", ".join(f"{k}: {v}" for k, v in anchors.items()))
    exe = find(game, "Zeus.exe")
    if exe:
        table, at = char_table(exe)
        used = [g for g in table if g]
        if max(used) > GLYPHS:
            failures.append(f"character table refers to glyph {max(used)}")
        letters = all(table[ord(c) - 0x20] == i + 1 for i, c in enumerate("abcdefghijklmnopqrstuvwxyz")) and \
            all(table[ord(c) - 0x20] == i + 27 for i, c in enumerate("ABCDEFGHIJKLMNOPQRSTUVWXYZ"))
        if not letters:
            failures.append("character table: a-z, A-Z are not glyphs 1-52")
        print(f"character table at file offset {at:#x}: {len(set(used))} glyphs used by {len(used)} characters")
    for f in failures:
        print("FAIL", f)
    return 1 if failures else 0


def cmd_table(path, codepage):
    table, at = char_table(executable(path))
    print(f"character table at file offset {at:#x} ({codepage})")
    for code in range(0x20, 0x100):
        g = table[code - 0x20]
        char = bytes([code]).decode(codepage, errors="replace")
        print(f"  {code:#04x} {char if char.isprintable() else ' '} -> {g if g else '-'}")


# Digits 3 x 5 pixels for the numbers of `sheet`.
DIGITS = ["111101101101111", "010110010010111", "111001111100111", "111001111001111", "101101111001001",
          "111100111001111", "111100111101111", "111001001001001", "111101111101111", "111101111001111"]


class Canvas:
    def __init__(self, w, h, colour=(40, 44, 52, 255)):
        self.w, self.h = w, h
        self.px = bytearray(bytes(colour) * (w * h))

    def put(self, x, y, rgba):
        if 0 <= x < self.w and 0 <= y < self.h and rgba[3]:
            o = (y * self.w + x) * 4
            self.px[o:o + 4] = bytes(rgba)

    def blit(self, rgba, w, h, x0, y0):
        for y in range(h):
            for x in range(w):
                o = (y * w + x) * 4
                self.put(x0 + x, y0 + y, rgba[o:o + 4])

    def number(self, n, x0, y0):
        for k, ch in enumerate(str(n)):
            for i, bit in enumerate(DIGITS[int(ch)]):
                if bit == "1":
                    self.put(x0 + 4 * k + i % 3, y0 + i // 3, (200, 200, 200, 255))


def cmd_sheet(spec, out, game):
    sg = font_file(game)
    rank, name, first = resolve(sg, spec)
    ims = sg.images[first:first + GLYPHS]
    cw = max(im.width for im in ims) + 6
    ch = max(im.height for im in ims) + 10
    cols = 10
    canvas = Canvas(cols * cw, (GLYPHS + cols - 1) // cols * ch)
    for g, im in enumerate(ims, start=1):
        x0, y0 = (g - 1) % cols * cw, (g - 1) // cols * ch
        canvas.number(g, x0 + 1, y0 + 1)
        rgba, w, h = sg.decode(im)
        canvas.blit(rgba, w, h, x0 + 3, y0 + 8)
    sg3.write_png(sg3.project_path(out), canvas.px, canvas.w, canvas.h)
    print(f"{name}: {GLYPHS} glyphs -> {out}")


def cmd_text(spec, text, out, game, codepage):
    sg = font_file(game)
    rank, name, first = resolve(sg, spec)
    table, _ = char_table(executable(game))
    line = LINE_HEIGHT.get(rank, 0x0B)
    placed, x = [], 0
    for code in text.encode(codepage, errors="replace"):
        if code == 0x20 or code == ord("_"):
            x += SPACE.get(rank, 6)
            continue
        g = table[code - 0x20] if code >= 0x20 else 0
        if not g:
            continue
        im = sg.images[first + g - 1]
        lift = max(0, im.height - line) if code >= 0x80 else 0  # without the edition's exceptions
        placed.append((im, x, -lift))
        x += im.width + EXTRA_PIXEL.get(rank, 0)
    top = min([y for _, _, y in placed] + [0])
    height = max([im.height + y for im, _, y in placed] + [1]) - top
    canvas = Canvas(max(x, 1) + 8, height + 8)
    for im, gx, gy in placed:
        rgba, w, h = sg.decode(im)
        canvas.blit(rgba, w, h, 4 + gx, 4 + gy - top)
    sg3.write_png(sg3.project_path(out), canvas.px, canvas.w, canvas.h)
    print(f"{name}: {len(placed)} glyphs -> {out}")


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd, args = argv[1], argv[2:]
    if cmd == "list":
        cmd_list(args[0] if args else DEFAULT_GAME)
    elif cmd == "check":
        return cmd_check(args[0] if args else DEFAULT_GAME)
    elif cmd == "table":
        cmd_table(args[0] if args else DEFAULT_GAME, args[1] if len(args) > 1 else "cp1250")
    elif cmd == "sheet" and len(args) >= 2:
        cmd_sheet(args[0], args[1], args[2] if len(args) > 2 else DEFAULT_GAME)
    elif cmd == "text" and len(args) >= 3:
        cmd_text(args[0], args[1], args[2], args[3] if len(args) > 3 else DEFAULT_GAME,
                 args[4] if len(args) > 4 else "cp1250")
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
