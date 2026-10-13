#!/usr/bin/env python3
"""Lecture des sprites du jeu : index `.sg3` + pixels `.555` (format décrit dans formats/sg3.md).

Usage :
    sg3.py info   FICHIER.sg3
    sg3.py check  DOSSIER                 décode toutes les images de tous les .sg3 du dossier
    sg3.py export FICHIER.sg3 DEST        un PNG par image (DEST : dossier vide, dans le projet)
    sg3.py sheet  FICHIER.sg3 SORTIE.png [PREMIER [NOMBRE]]   planche de contrôle
"""
import os
import struct
import sys
import zlib

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER_SIZE = 680
BITMAP_RECORDS, BITMAP_SIZE = 200, 200
TAG_RECORDS, TAG_SIZE = 300, 48
TILE_W, TILE_H = 58, 30
TILE_BYTES = 1800  # 900 pixels de 16 bits
ISO_KEY = 0x03E0   # vert pur : pixel de socle à ne pas dessiner (tuiles de relief)


class FormatError(Exception):
    pass


class Bitmap:
    __slots__ = ("index", "filename", "comment", "width", "height", "num_images", "start", "end")


class Image:
    __slots__ = ("index", "offset", "length", "uncompressed_length", "invert_offset", "width",
                 "height", "num_animation_sprites", "sprite_offset_x", "sprite_offset_y", "type",
                 "fully_compressed", "external", "has_compressed_part", "iso_size", "bitmap_id",
                 "alpha_field", "alpha_length", "raw")


class SgFile:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as fh:
            d = fh.read()
        (self.header_size, self.version, self.unknown, self.max_images, self.num_images,
         self.num_bitmaps, self.num_bitmaps_2, self.total_size, self.size_555,
         self.size_external) = struct.unpack_from("<10I", d, 0)
        if self.version not in (0xD5, 0xD6):
            raise FormatError(f"version inconnue {self.version:#x}")
        self.record_size = 72 if self.version == 0xD6 else 64
        self.groups = struct.unpack_from("<300H", d, 80)
        images_at = HEADER_SIZE + BITMAP_RECORDS * BITMAP_SIZE
        tags_at = images_at + self.max_images * self.record_size
        if len(d) != tags_at + TAG_RECORDS * TAG_SIZE:
            raise FormatError(f"taille de fichier inattendue {len(d)}")
        self.bitmaps = [self._bitmap(d, i) for i in range(self.num_bitmaps)]
        self.images = [self._image(d[images_at + self.record_size * i:
                                     images_at + self.record_size * (i + 1)], i)
                       for i in range(self.num_images + 1)]
        self.tags = [d[tags_at + TAG_SIZE * i:tags_at + TAG_SIZE * (i + 1)].split(b"\0")[0]
                     .decode("cp1252") for i in range(TAG_RECORDS)]
        self._pixels = {}

    @staticmethod
    def _bitmap(d, i):
        o = HEADER_SIZE + BITMAP_SIZE * i
        b = Bitmap()
        b.index = i
        b.filename = d[o:o + 65].split(b"\0")[0].decode("cp1252")
        b.comment = d[o + 65:o + 116].split(b"\0")[0].decode("cp1252")
        b.width, b.height, b.num_images, b.start, b.end = struct.unpack_from("<5I", d, o + 116)
        return b

    def _image(self, r, i):
        im = Image()
        im.index = i
        im.raw = r
        im.offset, im.length, im.uncompressed_length, _, im.invert_offset = \
            struct.unpack_from("<IIIIi", r, 0)
        im.width, im.height = struct.unpack_from("<hh", r, 20)
        im.num_animation_sprites, = struct.unpack_from("<H", r, 30)
        im.sprite_offset_x, im.sprite_offset_y = struct.unpack_from("<hh", r, 34)
        im.type = r[50]
        im.fully_compressed, im.external, im.has_compressed_part, im.iso_size = \
            r[51], r[52], r[53], r[55]
        im.bitmap_id = r[56]
        im.alpha_field, im.alpha_length = struct.unpack_from("<II", r, 64) \
            if self.record_size == 72 else (0, 0)
        return im

    # --- accès aux pixels -------------------------------------------------------------

    def _data_file(self, im):
        folder = os.path.dirname(self.path)
        if im.external:
            name = os.path.splitext(self.bitmaps[im.bitmap_id].filename)[0] + ".555"
        else:
            name = os.path.splitext(os.path.basename(self.path))[0] + ".555"
        if name not in self._pixels:
            real = {n.lower(): n for n in os.listdir(folder)}.get(name.lower())
            if real is None:
                raise FormatError(f"fichier de pixels introuvable : {name}")
            with open(os.path.join(folder, real), "rb") as fh:
                self._pixels[name] = fh.read()
        return self._pixels[name]

    def data(self, im):
        """Octets stockés de l'image (pixels, puis masque alpha en version 0xD6)."""
        buf = self._data_file(im)
        start = im.offset - (1 if im.external else 0)
        end = start + im.length + im.alpha_length
        if end > len(buf):
            raise FormatError(f"image {im.index} : données hors fichier ({end} > {len(buf)})")
        return buf[start:end]

    def source(self, im):
        """Image réellement stockée : suit le renvoi des images miroir."""
        if im.invert_offset:
            return self.images[im.index + im.invert_offset]
        return im

    def decode(self, im):
        """Pixels RGBA (bytearray de width*height*4), origine en haut à gauche."""
        mirrored = bool(im.invert_offset)
        src = self.source(im)
        w, h = src.width, src.height
        if w <= 0 or h <= 0:
            if src.length:
                raise FormatError(f"image {im.index} : dimensions {w}x{h} avec {src.length} octets")
            return bytearray(), 0, 0
        out = bytearray(w * h * 4)
        if src.length == 0:
            return out, w, h
        d = self.data(src)
        pix, alpha = d[:src.length], d[src.length:]
        if src.type == 30:
            _iso_base(out, w, h, pix[:src.uncompressed_length], src)
            if src.has_compressed_part or src.length > src.uncompressed_length:
                _runs(out, w, h, pix[src.uncompressed_length:], src)
        elif src.fully_compressed:
            _runs(out, w, h, pix, src)
        else:
            _plain(out, w, h, pix, src)
        if alpha:
            _alpha(out, w, h, alpha, src)
        if mirrored:
            row = w * 4
            for y in range(h):
                line = out[y * row:(y + 1) * row]
                out[y * row:(y + 1) * row] = b"".join(line[x:x + 4] for x in range(row - 4, -4, -4))
        return out, w, h


def _put(out, pos, c):
    """Écrit une couleur 15 bits (xRRRRRGGGGGBBBBB) à l'indice de pixel pos."""
    r, g, b = (c >> 10) & 31, (c >> 5) & 31, c & 31
    o = pos * 4
    out[o], out[o + 1], out[o + 2], out[o + 3] = r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2, 255


def _plain(out, w, h, pix, im):
    if len(pix) != w * h * 2:
        raise FormatError(f"image {im.index} : {len(pix)} octets pour {w}x{h} non compressé")
    for pos, (c,) in enumerate(struct.iter_unpack("<H", pix)):
        if c != 0xF81F:  # couleur réservée à la transparence
            _put(out, pos, c)


def _runs(out, w, h, pix, im):
    """Suites : 255,n = n pixels transparents ; sinon c = c pixels de 16 bits qui suivent."""
    i = pos = 0
    total, n = w * h, len(pix)
    while i < n:
        c = pix[i]
        i += 1
        if c == 255:
            pos += pix[i]
            i += 1
        else:
            if i + 2 * c > n or pos + c > total:
                raise FormatError(f"image {im.index} : suite hors limites")
            for k in range(c):
                _put(out, pos + k, pix[i] | pix[i + 1] << 8)
                i += 2
            pos += c
    if pos > total:
        raise FormatError(f"image {im.index} : {pos} pixels décodés pour {total}")


def _alpha(out, w, h, data, im):
    """Masque alpha (0xD6) : mêmes suites, un octet d'opacité sur 5 bits par pixel."""
    i = pos = 0
    total, n = w * h, len(data)
    while i < n:
        c = data[i]
        i += 1
        if c == 255:
            pos += data[i]
            i += 1
        else:
            if i + c > n or pos + c > total:
                raise FormatError(f"image {im.index} : masque alpha hors limites")
            for k in range(c):
                a = data[i + k]
                if a > 31:
                    raise FormatError(f"image {im.index} : alpha {a} > 31")
                out[(pos + k) * 4 + 3] = a << 3 | a >> 2
            i += c
            pos += c


def _iso_base(out, w, h, pix, im):
    """Socle isométrique : losanges de 58x30 rangés par lignes, du haut vers le bas."""
    base_h = (w + 2) // 2
    size = im.iso_size or base_h // TILE_H
    if size * TILE_H != base_h or (w + 2) * base_h != len(pix) or len(pix) != size * size * TILE_BYTES:
        raise FormatError(f"image {im.index} : socle {w}x{h} taille {size}, {len(pix)} octets")
    top = h - base_h
    i = 0
    for row in range(2 * size - 1):
        count = row + 1 if row < size else 2 * size - row - 1
        x0 = (size - row - 1 if row < size else row - size + 1) * TILE_H
        y0 = top + row * TILE_H // 2
        for k in range(count):
            tx = x0 + k * (TILE_W + 2)
            for y in range(TILE_H):
                start = TILE_H - 2 * (y + 1) if y < TILE_H // 2 else 2 * y - TILE_H
                for x in range(start, TILE_W - start):
                    c = pix[i] | pix[i + 1] << 8
                    if c & 0x7FFF != ISO_KEY:  # couleur-clé : le socle laisse voir le dessous
                        _put(out, (y0 + y) * w + tx + x, c)
                    i += 2


# --- sorties ----------------------------------------------------------------------------

def write_png(path, rgba, w, h):
    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))
    rows = b"".join(b"\0" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    with open(path, "wb") as fh:
        fh.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                 + chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))


def project_path(path, must_be_empty_dir=False):
    path = os.path.abspath(path)
    if os.path.commonpath([path, PROJECT_ROOT]) != PROJECT_ROOT or path == PROJECT_ROOT:
        sys.exit(f"refus : {path} n'est pas dans le projet")
    if must_be_empty_dir and os.path.exists(path) and os.listdir(path):
        sys.exit(f"refus : {path} existe et n'est pas vide")
    return path


def cmd_info(path):
    sg = SgFile(path)
    print(f"{os.path.basename(path)} : version {sg.version:#x}, {sg.num_images} images "
          f"(max {sg.max_images}), {sg.num_bitmaps} bitmaps, .555 = {sg.size_555} octets")
    for b in sg.bitmaps:
        print(f"  bitmap {b.index:3} {b.filename:32} images {b.start}-{b.end} ({b.num_images})  {b.comment}")
    print("  groupes nommés :", ", ".join(f"{t}={sg.groups[i]}" for i, t in enumerate(sg.tags)
                                           if t and sg.groups[i]))


def cmd_check(folder):
    names = sorted(n for n in os.listdir(folder) if n.lower().endswith(".sg3"))
    grand_ok = grand_bad = 0
    for name in names:
        try:
            sg = SgFile(os.path.join(folder, name))
        except (FormatError, OSError) as exc:
            print(f"ECHEC {name} : {exc}")
            grand_bad += 1
            continue
        ok = bad = 0
        first = None
        for im in sg.images[1:]:
            try:
                sg.decode(im)
                ok += 1
            except (FormatError, IndexError, struct.error) as exc:
                bad += 1
                first = first or f"{type(exc).__name__}: {exc}"
        grand_ok += ok
        grand_bad += bad
        if bad:
            print(f"ECHEC {name:34} {ok:5} ok, {bad:5} en échec ; premier : {first}")
    print(f"{len(names)} fichiers, {grand_ok} images décodées, {grand_bad} échecs")


def cmd_export(path, dest):
    dest = project_path(dest, must_be_empty_dir=True)
    os.makedirs(dest, exist_ok=True)
    sg = SgFile(path)
    count = 0
    for im in sg.images[1:]:
        rgba, w, h = sg.decode(im)
        if w and h:
            write_png(os.path.join(dest, f"{im.index:05d}.png"), rgba, w, h)
            count += 1
    print(f"{count} images écrites dans {dest}")


def cmd_sheet(path, out, first=1, count=200, sheet_w=1600):
    out = project_path(out)
    sg = SgFile(path)
    placed, x, y, row_h = [], 4, 4, 0
    for im in sg.images[first:first + count]:
        rgba, w, h = sg.decode(im)
        if not w or not h or w > sheet_w - 8:
            continue
        if x + w + 4 > sheet_w:
            x, y, row_h = 4, y + row_h + 4, 0
        placed.append((x, y, w, h, rgba))
        x += w + 4
        row_h = max(row_h, h)
    sheet_h = y + row_h + 4
    sheet = bytearray(b"\x30\x34\x3c\xff" * (sheet_w * sheet_h))
    for px, py, w, h, rgba in placed:
        for yy in range(h):
            for xx in range(w):
                s = (yy * w + xx) * 4
                a = rgba[s + 3]
                if a:
                    t = ((py + yy) * sheet_w + px + xx) * 4
                    for k in range(3):
                        sheet[t + k] = (rgba[s + k] * a + sheet[t + k] * (255 - a)) // 255
    os.makedirs(os.path.dirname(out), exist_ok=True)
    write_png(out, sheet, sheet_w, sheet_h)
    print(f"{len(placed)} images sur {out} ({sheet_w}x{sheet_h})")


def main(argv):
    if len(argv) == 3 and argv[1] == "info":
        cmd_info(argv[2])
    elif len(argv) == 3 and argv[1] == "check":
        cmd_check(argv[2])
    elif len(argv) == 4 and argv[1] == "export":
        cmd_export(argv[2], argv[3])
    elif len(argv) in (4, 5, 6) and argv[1] == "sheet":
        cmd_sheet(argv[2], argv[3], *map(int, argv[4:]))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
