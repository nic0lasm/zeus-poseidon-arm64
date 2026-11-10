#!/usr/bin/env python3
"""Another alphabet in the fonts of Zeus + Poseidon, drawn from skeleton letters.

    alphabet.py build GAME_DIR LANG_DIR PACK_DIR
        writes PACK_DIR/DATA/Zeus_Fonts.sg3 and zeus_Fonts.555 with the new letters,
        PACK_DIR/table-caracteres.bin (character -> glyph table for the language's code page),
        PACK_DIR/codage.txt (the code page) and PACK_DIR/remontee.txt ("ancre": the host takes
        each glyph's vertical lift from its anchor, docs/languages.md)
    alphabet.py sheet GAME_DIR LANG_DIR OUT.png [TEXT]
        a preview: TEXT (or a few sample lines) in every font, enlarged three times

GAME_DIR is the player's own installation (only read). LANG_DIR describes the language:
alphabet.txt (code page, new letters, letters shared with Latin, special table entries) and
skeletons.txt (the new letters, one pixel wide); tools/examples/lang/uk is the Ukrainian one.
The result is made of game files: it stays on the player's computer and must not be shared.

How a glyph is made. The game's glyphs are a dark one-pixel stroke (two pixels in the 22 pt
fonts) and a light copy of it shifted by (+1, +1) ((+1, 0) in 9B and 9LTBK); the masked font
SPR_FONTS is the stroke widened by one pixel to the right, which the game colours itself. Each
pixel of a new glyph takes the colour of the pixel of the same font's Latin letters whose 5 × 5
neighbourhood is the most alike (with its dark or light class and its height above the
baseline), which keeps each of the 17 fonts' texture and shading. The new letters take slots 81
to 134 (the accented Western letters; slot 130, the degree sign, is kept), in the order upper
case, lower case, signs. A glyph that reaches above the font's lines is lifted, and the lift is
written in its record (anchor y, +36), which the host reads when remontee.txt says "ancre".

Rewriting the fonts: the pixel data must stay contiguous in record order (the game does not
accept data appended at the end), and each row of a compressed image is encoded on its own,
colour runs of at most 16 pixels.
"""
import os
import struct
import sys
import zlib

HEADER = 680
RECORDS_AT = 680 + 200 * 200
RECORD = 64  # version 0xD5

FONT_NAMES = ["SPR_FONTS", "SPR_FONTS_11C", "SPR_FONTS_11C4", "SPR_FONTS_11C2", "SPR_FONTS_11C3", "SPR_FONTS_11CB",
              "SPR_FONTS_22C", "SPR_FONTS_22C2", "SPR_FONTS_9B", "SPR_FONTS_11LTYEL", "SPR_FONTS_9LTBK",
              "SPR_FONTS_9LTBU", "SPR_FONTS_9LTYEL", "SPR_FONTS_9DKBU", "SPR_FONTS_9DKYEL", "SPR_FONTS_11LTBLUE",
              "SPR_FONTS_DKMESSAGE"]
ELEVEN = ["SPR_FONTS_11C", "SPR_FONTS_11C2", "SPR_FONTS_11C3", "SPR_FONTS_11C4", "SPR_FONTS_11CB",
          "SPR_FONTS_11LTYEL", "SPR_FONTS_11LTBLUE"]
# font: (skeleton set for capitals, set for small letters, scale, shift of the light copy, or
# None for the masked font)
FAMILY = {n: ("A", "B", 1, (1, 1)) for n in ELEVEN}
FAMILY.update({
    "SPR_FONTS": ("B", "C", 1, None),
    "SPR_FONTS_22C": ("A", "B", 2, (1, 1)), "SPR_FONTS_22C2": ("A", "B", 2, (1, 1)),
    "SPR_FONTS_9B": ("B", "C", 1, (1, 0)), "SPR_FONTS_9LTBK": ("B", "C", 1, (1, 0)),
    "SPR_FONTS_9LTBU": ("B", "C", 1, (1, 1)), "SPR_FONTS_9LTYEL": ("B", "C", 1, (1, 1)),
    "SPR_FONTS_9DKBU": ("B", "C", 1, (1, 1)), "SPR_FONTS_9DKYEL": ("B", "C", 1, (1, 1)),
    "SPR_FONTS_DKMESSAGE": ("C", "C", 1, (1, 1)),
})
SLOTS = [g for g in range(81, 135) if g != 130]


# --- language description -------------------------------------------------------------------

class Language:
    def __init__(self, folder):
        self.values = {}
        with open(os.path.join(folder, "alphabet.txt"), encoding="utf-8") as f:
            for line in f:
                line = line.rstrip("\n")
                if not line.strip() or line.lstrip().startswith("#"):
                    continue
                key, _, value = line.partition(":")
                self.values[key.strip()] = value.strip()
        self.code_page = int(self.values["code_page"])
        self.codec = "cp%d" % self.code_page
        upper = self.values["new_upper"]
        self.new = [(c, "U") for c in upper] + [(c.lower(), "L") for c in upper] + \
                   [(c, "U") for c in self.values.get("signs", "")]
        self.same = {}
        for pair in self.values.get("same_as_latin", "").split():
            a, _, b = pair.partition("=")
            self.same[a] = b
            self.same[a.lower()] = b.lower()
        self.thick = set(self.values.get("thick", ""))
        self.table_entries = []
        for item in self.values.get("table", "").split():
            code, _, target = item.partition("=")
            self.table_entries.append((int(code, 0), target))
        self.sets = read_skeletons(os.path.join(folder, "skeletons.txt"))


def read_skeletons(path):
    """{set: {letter: text}} from skeletons.txt."""
    sets, current, rows = {}, None, []

    def flush():
        if current is not None:
            text = "\n".join(rows).strip("\n")
            sets.setdefault(current[0], {})[current[1]] = text

    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("== "):
                flush()
                _, set_name, letter = line.split(" ", 2)
                current, rows = (set_name, letter), []
            elif current is not None:
                rows.append(line)
    flush()
    return sets


def parse_skeleton(text):
    """-> (rows of booleans, index of the baseline row)."""
    rows = text.strip("\n").split("\n")
    if "-" in rows:
        cut = rows.index("-")
        body, below = rows[:cut], rows[cut + 1:]
    else:
        body, below = rows, []
    every = body + below
    width = max(len(r) for r in every)
    return [[c == "#" for c in r.ljust(width, ".")] for r in every], len(body) - 1


# --- fonts file -----------------------------------------------------------------------------

class FontBank:
    """Zeus_Fonts.sg3 / zeus_Fonts.555: images as rows of raw 16-bit colours, -1 = transparent."""

    def __init__(self, sg3_path, p555_path):
        with open(sg3_path, "rb") as f:
            self.sg3 = bytearray(f.read())
        with open(p555_path, "rb") as f:
            self.p555 = bytearray(f.read())
        if struct.unpack_from("<I", self.sg3, 4)[0] != 0xD5:
            raise SystemExit("%s: version 0xD5 expected" % sg3_path)
        self.max_images = struct.unpack_from("<I", self.sg3, 12)[0]
        self.groups = struct.unpack_from("<300H", self.sg3, 80)
        tags_at = RECORDS_AT + self.max_images * RECORD
        self.tags = [bytes(self.sg3[tags_at + 48 * i:tags_at + 48 * (i + 1)]).split(b"\0")[0].decode("cp1252")
                     for i in range(300)]
        self.new_data = {}

    def first(self, name):
        return self.groups[self.tags.index(name)]

    def record_at(self, index):
        return RECORDS_AT + index * RECORD

    def decode(self, index):
        at = self.record_at(index)
        offset, length = struct.unpack_from("<II", self.sg3, at)
        if struct.unpack_from("<i", self.sg3, at + 16)[0] != 0:
            raise SystemExit("image %d is a mirror: not expected in the fonts" % index)
        w, h = struct.unpack_from("<hh", self.sg3, at + 20)
        flat = [-1] * (w * h)
        d = self.p555[offset:offset + length]
        if not self.sg3[at + 51]:
            values = struct.unpack_from("<%dH" % (w * h), d, 0)
            flat = [-1 if v == 0xF81F else v for v in values]
        else:
            i = pos = 0
            while i < len(d):
                c = d[i]
                i += 1
                if c == 255:
                    pos += d[i]
                    i += 1
                else:
                    flat[pos:pos + c] = struct.unpack_from("<%dH" % c, d, i)
                    i += 2 * c
                    pos += c
        return [flat[y * w:(y + 1) * w] for y in range(h)]

    @staticmethod
    def encode(image):
        """Run-length encoding as the game reads it, each row on its own: 255, n = n transparent
        pixels; c (1 to 16) = c colours."""
        out = bytearray()
        for row in image:
            i, n = 0, len(row)
            while i < n:
                j = i
                if row[i] < 0:
                    while j < n and row[j] < 0 and j - i < 255:
                        j += 1
                    out += bytes([255, j - i])
                else:
                    while j < n and row[j] >= 0 and j - i < 16:
                        j += 1
                    out.append(j - i)
                    out += struct.pack("<%dH" % (j - i), *row[i:j])
                i = j
        return bytes(out)

    def replace(self, index, image):
        at = self.record_at(index)
        data = self.encode(image)
        self.new_data[index] = data
        h, w = len(image), len(image[0])
        struct.pack_into("<III", self.sg3, at, 0, len(data), len(data))
        struct.pack_into("<i", self.sg3, at + 16, 0)
        struct.pack_into("<hh", self.sg3, at + 20, w, h)
        self.sg3[at + 51] = 1  # compressed
        self.sg3[at + 62] = 0

    def save(self, sg3_path, p555_path):
        """Rewrites the .555 contiguous: its 4 lead bytes, then the data of images 1 to N in order."""
        n = struct.unpack_from("<I", self.sg3, 16)[0]
        out = bytearray(self.p555[:4])
        header = bytearray(self.sg3)
        for i in range(1, n + 1):
            at = self.record_at(i)
            offset, length = struct.unpack_from("<II", self.sg3, at)
            data = self.new_data[i] if i in self.new_data else bytes(self.p555[offset:offset + length])
            if not data:
                continue  # an empty image keeps its offset
            struct.pack_into("<I", header, at, len(out))
            out += data
        total, size_555 = struct.unpack_from("<II", self.sg3, 28)
        struct.pack_into("<II", header, 28, total + (len(out) - 4 - size_555), len(out) - 4)
        with open(sg3_path, "wb") as f:
            f.write(header)
        with open(p555_path, "wb") as f:
            f.write(out)


def find_file(folder, name):
    """A file of the installation, whatever the case of its name."""
    for entry in os.listdir(folder):
        if entry.lower() == name.lower():
            return os.path.join(folder, entry)
    raise SystemExit("%s not found in %s" % (name, folder))


def game_table(game):
    """The 224-byte character -> glyph table of the executable (characters 0x20 to 0xFF), found by
    its run of letters: a to z are glyphs 1 to 26, A to Z glyphs 27 to 52."""
    with open(find_file(game, "zeus.exe"), "rb") as f:
        exe = f.read()
    letters = bytes(range(27, 53))
    at = exe.find(letters)
    while at >= 0:
        start = at - 0x41
        if exe[start + 0x61:start + 0x7B] == bytes(range(1, 27)):
            return exe[start + 0x20:start + 0x100]
        at = exe.find(letters, at + 1)
    raise SystemExit("character table not found in the executable")


# --- arithmetic as numpy does it (the reference tool was written with numpy) ---------------------

def pairwise_sum(a, lo, n):
    """numpy's pairwise summation of float64 values: the same rounding as numpy's mean."""
    if n < 8:
        total = 0.0
        for i in range(lo, lo + n):
            total += a[i]
        return total
    if n <= 128:
        r = [a[lo + j] for j in range(8)]
        i = 8
        while i < n - n % 8:
            for j in range(8):
                r[j] += a[lo + i + j]
            i += 8
        total = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]))
        while i < n:
            total += a[lo + i]
            i += 1
        return total
    half = n // 2
    half -= half % 8
    return pairwise_sum(a, lo, half) + pairwise_sum(a, lo + half, n - half)


def mean(values):
    return pairwise_sum(values, 0, len(values)) / len(values) if values else float("nan")


def luminance(v):
    v = max(v, 0)
    r, g, b = (v >> 10) & 31, (v >> 5) & 31, v & 31
    return 0.3 * r + 0.59 * g + 0.11 * b


def otsu(values):
    """Threshold that best separates dark and light values (Otsu)."""
    values = sorted(values)
    best, threshold = -1, mean(values)
    n = len(values)
    k = 0
    for t in sorted(set(values)):
        while k < n and values[k] <= t:
            k += 1
        if k == 0 or k == n:
            continue
        a, b = values[:k], values[k:]
        s = len(a) * len(b) * (mean(a) - mean(b)) ** 2
        if s > best:
            best, threshold = s, t
    return threshold


# --- glyphs -----------------------------------------------------------------------------------

# Number of set bits (int.bit_count from Python 3.10 on).
popcount = int.bit_count if hasattr(int, "bit_count") else (lambda n: bin(n).count("1"))


def latin_glyph(ch):
    return ord(ch) - 96 if ch.islower() else ord(ch) - 64 + 26


def window_code(mask, h, w, y, x):
    """The 5 × 5 neighbourhood of (y, x) as 25 bits, row by row."""
    code, bit = 0, 0
    for yy in range(y - 2, y + 3):
        row = mask[yy] if 0 <= yy < h else None
        for xx in range(x - 2, x + 3):
            if row is not None and 0 <= xx < w and row[xx]:
                code |= 1 << bit
            bit += 1
    return code


def skeleton(language, ch, case, family):
    upper_set, lower_set, scale, _ = family
    key = ch.upper() if ch.upper() in language.sets["A"] else ch
    grid, base = parse_skeleton(language.sets[upper_set if case == "U" else lower_set][key])
    s = [row[:] for row in grid]
    if key in language.thick and scale == 1:
        # Steep diagonals: as in Latin A and V, two dark pixels in each row. Horizontal strokes
        # (runs of three pixels or more, the foot of Д) are left alone.
        h, w = len(s), len(s[0])
        run = [[False] * w for _ in range(h)]
        for y in range(h):
            x = 0
            while x < w:
                if s[y][x]:
                    e = x
                    while e < w and s[y][e]:
                        e += 1
                    if e - x >= 3:
                        for k in range(x, e):
                            run[y][k] = True
                    x = e
                else:
                    x += 1
        wide = []
        for y in range(h):
            row = s[y] + [False]
            for x in range(w):
                if s[y][x] and not run[y][x]:
                    row[x + 1] = True
            wide.append(row)
        s = wide if any(row[w] for row in wide) else [row[:w] for row in wide]
    if scale == 2:
        s = [[v for v in row for _ in (0, 1)] for row in s for _ in (0, 1)]
        base = base * 2 + 1
    return s, base


class Style:
    """The colours of one font: every pixel of its Latin letters with its neighbourhood."""

    def __init__(self, bank, name, case, shift):
        first = bank.first(name)
        glyph_numbers = range(27, 63) if case == "U" else range(1, 27)
        images = [bank.decode(first + g - 1) for g in glyph_numbers]
        reference = bank.decode(first + latin_glyph("H" if case == "U" else "h") - 1)
        dy = shift[1] if shift else 0
        self.base = max(y for y, row in enumerate(reference) if any(v >= 0 for v in row)) - dy
        self.mask_mode = shift is None
        values = [v for image in images for row in image for v in row if v >= 0]
        lums = [luminance(v) for v in values]
        self.threshold = otsu(lums)
        # Light text (the LT* fonts, 11C2): the body of the letter is the light layer, lying over
        # the dark one; in the dark fonts the other way round. The layer further from the middle
        # brightness tells which.
        light = mean([v for v in lums if v > self.threshold])
        dark = mean([v for v in lums if v <= self.threshold])
        self.light_text = (light - 15.5) > (15.5 - dark)
        if self.mask_mode:
            counts = {}
            for v in values:
                counts[v] = counts.get(v, 0) + 1
            self.colour = min(counts, key=lambda v: (-counts[v], v))
            return
        seen, self.candidates = set(), []
        for image in images:
            h, w = len(image), len(image[0])
            mask = [[v >= 0 for v in row] for row in image]
            for y in range(h):
                for x in range(w):
                    if mask[y][x]:
                        v = image[y][x]
                        key = (window_code(mask, h, w, y, x), luminance(v) <= self.threshold, y - self.base)
                        if key not in seen:  # the first of equal candidates is the one that wins
                            seen.add(key)
                            self.candidates.append(key + (v,))
        self.cache = {}

    def colour_of(self, code, dark, row):
        key = (code, dark, row)
        if key not in self.cache:
            best, colour = None, None
            for feature, is_dark, r, v in self.candidates:
                d = popcount(feature ^ code) * 2.0 + (20.0 if is_dark != dark else 0.0) + abs(r - row) * 0.6
                if best is None or d < best:
                    best, colour = d, v
            self.cache[key] = colour
        return self.cache[key]


def render(style, s, base, shift):
    """Skeleton -> (image of raw colours, lift)."""
    sh, sw = len(s), len(s[0])
    if shift == (1, 1) and sh < 20:
        # A "\" diagonal: the light copy would fall on the stroke itself and leave it thin; as in
        # the Latin N, each step of the diagonal gets a neighbour on its right.
        step = [[False] * sw for _ in range(sh)]
        for y in range(sh - 1):
            for x in range(sw - 1):
                step[y][x] = s[y][x] and s[y + 1][x + 1] and not s[y][x + 1] and not s[y + 1][x]
        s = [row[:] for row in s]
        for y in range(sh):
            for x in range(1, sw):
                if step[y][x - 1]:
                    s[y][x] = True
    top = style.base - base  # image row of the skeleton's top row
    lift = max(0, -top)
    top += lift
    dx, dy = shift if shift else (1, 0)
    h, w = top + sh + dy, sw + dx
    dark = [[False] * w for _ in range(h)]
    for y in range(sh):
        for x in range(sw):
            dark[top + y][x] = s[y][x]
    full = [row[:] for row in dark]
    for y in range(sh):
        for x in range(sw):
            if s[y][x]:
                full[top + dy + y][dx + x] = True
    if style.light_text and shift is not None and sh < 20:
        # As in the Latin glyphs of the 9 and 11 pt fonts: the light copy lies over the dark one,
        # which shows only where the light does not cover it.
        for y in range(sh):
            for x in range(sw):
                if s[y][x]:
                    dark[top + dy + y][dx + x] = False
    image = [[-1] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if full[y][x]:
                image[y][x] = style.colour if style.mask_mode else \
                    style.colour_of(window_code(full, h, w, y, x), dark[y][x], y - (style.base + lift))
    return image, lift


def build_glyphs(bank, language):
    images = {}  # (font, slot) -> (image, lift)
    for name in FONT_NAMES:
        family = FAMILY[name]
        styles = {c: Style(bank, name, c, family[3]) for c in "UL"}
        for (ch, case), slot in zip(language.new, SLOTS):
            s, base = skeleton(language, ch, case, family)
            images[(name, slot)] = render(styles[case], s, base, family[3])
    return images


def character_table(game, language):
    table = bytearray(game_table(game))
    for code in range(0x80, 0x100):
        table[code - 0x20] = 0
    slot_of = {ch: slot for (ch, _), slot in zip(language.new, SLOTS)}
    for code in range(0x80, 0x100):
        ch = bytes([code]).decode(language.codec, errors="replace")
        if ch in slot_of:
            table[code - 0x20] = slot_of[ch]
        elif ch in language.same:
            table[code - 0x20] = latin_glyph(language.same[ch])
    for code, target in language.table_entries:
        if target.startswith("#"):
            table[code - 0x20] = int(target[1:])
        else:
            table[code - 0x20] = table[target.encode(language.codec)[0] - 0x20]
    return bytes(table)


def open_bank(game):
    data = find_file(game, "DATA")
    return FontBank(find_file(data, "Zeus_Fonts.sg3"), find_file(data, "zeus_Fonts.555"))


def cmd_build(game, folder, pack):
    language = Language(folder)
    bank = open_bank(game)
    images = build_glyphs(bank, language)
    for name in FONT_NAMES:
        first = bank.first(name)
        for g in range(81, 135):
            index = first + g - 1
            at = bank.record_at(index)
            if (name, g) in images:
                image, lift = images[(name, g)]
                bank.replace(index, image)
                struct.pack_into("<h", bank.sg3, at + 36, lift)
            else:
                struct.pack_into("<h", bank.sg3, at + 36, 0)
    os.makedirs(os.path.join(pack, "DATA"), exist_ok=True)
    bank.save(os.path.join(pack, "DATA", "Zeus_Fonts.sg3"), os.path.join(pack, "DATA", "zeus_Fonts.555"))
    with open(os.path.join(pack, "table-caracteres.bin"), "wb") as f:
        f.write(character_table(game, language))
    with open(os.path.join(pack, "codage.txt"), "w") as f:
        f.write("%d\n" % language.code_page)
    with open(os.path.join(pack, "remontee.txt"), "w") as f:
        f.write("ancre\n")
    print("fonts: %d new glyphs in %d fonts -> %s" % (len(images), len(FONT_NAMES), pack))


# --- preview ----------------------------------------------------------------------------------

def write_png(path, width, height, rgb_rows):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    raw = b"".join(b"\0" + bytes(row) for row in rgb_rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def cmd_sheet(game, folder, out, texts=None):
    language = Language(folder)
    bank = open_bank(game)
    images = build_glyphs(bank, language)
    table = character_table(game, language)
    texts = texts or ["".join(c for c, case in language.new if case == "U") + "АВКМНОРСТХІ",
                      "".join(c for c, case in language.new if case == "L"),
                      "Zeus + Poseidon, 1234567890"]
    scale, pad = 3, 10
    lines = []
    for name in FONT_NAMES:
        first = bank.first(name)
        for text in texts:
            glyphs = []
            for ch in text:
                code = ch.encode(language.codec, errors="replace")[0]
                g = table[code - 0x20] if code >= 0x20 else 0
                if code == 0x20 or g == 0:
                    glyphs.append(None)
                elif (name, g) in images:
                    glyphs.append(images[(name, g)])
                else:
                    glyphs.append((bank.decode(first + g - 1), 0))
            lines.append((name, glyphs))
    line_height = lambda name: 48 if "22" in name else 26  # noqa: E731
    width = max(sum(len(g[0][0]) + 1 if g else 6 for g in glyphs) for _, glyphs in lines) * scale + 2 * pad
    height = sum(line_height(name) for name, _ in lines) * scale + 2 * pad
    canvas = [[(96, 80, 60)] * width for _ in range(height)]
    y0 = pad
    for name, glyphs in lines:
        x0 = pad
        for g in glyphs:
            if g is None:
                x0 += 6 * scale
                continue
            image, lift = g
            for y, row in enumerate(image):
                for x, v in enumerate(row):
                    if v < 0:
                        continue
                    colour = (240, 230, 200) if name == "SPR_FONTS" else \
                        (((v >> 10) & 31) << 3, ((v >> 5) & 31) << 3, (v & 31) << 3)
                    for yy in range(scale):
                        for xx in range(scale):
                            py, px = y0 + (8 - lift + y) * scale + yy, x0 + x * scale + xx
                            if 0 <= py < height and 0 <= px < width:
                                canvas[py][px] = colour
            x0 += (len(image[0]) + 1) * scale
        y0 += line_height(name) * scale
    write_png(out, width, height, [[c for pixel in row for c in pixel] for row in canvas])
    print(out)


def main(argv):
    if len(argv) == 5 and argv[1] == "build":
        cmd_build(argv[2], argv[3], argv[4])
    elif len(argv) in (5, 6) and argv[1] == "sheet":
        cmd_sheet(argv[2], argv[3], argv[4], [argv[5]] if len(argv) == 6 else None)
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
