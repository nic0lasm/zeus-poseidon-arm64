#!/usr/bin/env python3
"""Assemble des captures BMP en une planche, pour les relire d'un coup d'œil.

Usage : python3 tools/contact_sheet.py SORTIE.bmp COLONNES CAPTURE.bmp [CAPTURE.bmp …]

Chaque capture (BMP 24 ou 32 bits, comme celles de zeus-run --screenshot ou de l'action
« shot » d'un script) est réduite de moitié puis placée dans une grille de COLONNES colonnes,
dans l'ordre des arguments. La planche est un BMP 24 bits ; la convertir au besoin avec
`sips -s format png -Z 1200 SORTIE.bmp --out SORTIE.png`. Bibliothèque standard seulement.
"""
import struct
import sys


def load(path):
    """Renvoie (largeur, hauteur, octets par pixel, lignes de haut en bas)."""
    with open(path, "rb") as f:
        d = f.read()
    off = struct.unpack_from("<I", d, 10)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    bpp = struct.unpack_from("<H", d, 28)[0]
    B = bpp // 8
    row = (w * B + 3) // 4 * 4
    flip = h > 0  # hauteur positive : lignes rangées de bas en haut
    h = abs(h)
    rows = [d[off + y * row: off + y * row + w * B] for y in range(h)]
    if flip:
        rows.reverse()
    return w, h, B, rows


def main(argv):
    if len(argv) < 4:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    out, cols, files = argv[1], int(argv[2]), argv[3:]
    tiles = []
    for name in files:
        w, h, B, rows = load(name)
        small = [b"".join(r[x * B:x * B + 3] for x in range(0, w, 2)) for r in rows[::2]]
        tiles.append((w // 2, h // 2, small))
    tw = max(t[0] for t in tiles)
    th = max(t[1] for t in tiles)
    n = (len(tiles) + cols - 1) // cols
    W, H = tw * cols, th * n
    canvas = [bytearray(W * 3) for _ in range(H)]
    for i, (w, h, small) in enumerate(tiles):
        ox, oy = (i % cols) * tw, (i // cols) * th
        for y in range(h):
            canvas[oy + y][ox * 3:ox * 3 + w * 3] = small[y][:w * 3]
    row = (W * 3 + 3) // 4 * 4
    with open(out, "wb") as f:
        f.write(b"BM" + struct.pack("<IHHI", 54 + row * H, 0, 0, 54)
                + struct.pack("<IiiHHIIiiII", 40, W, -H, 1, 24, 0, row * H, 2835, 2835, 0, 0))
        for r in canvas:
            f.write(bytes(r) + b"\0" * (row - W * 3))
    print(f"{out} : {len(tiles)} captures, {W} x {H}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
