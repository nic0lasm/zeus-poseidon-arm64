#!/usr/bin/env python3
"""Lecture des fichiers de carte et de sauvegarde du jeu (conteneur « MAPS », voir formats/map.md).

Usage :
    mapfile.py info  FICHIER             table des blocs
    mapfile.py check DOSSIER             lit tous les .map/.sav/.pak/.set trouvés sous DOSSIER
    mapfile.py dump  FICHIER DEST        écrit chaque bloc décompressé (DEST vide, dans le projet)
    mapfile.py grids FICHIER DEST        exporte en PNG les blocs qui ont la taille d'une grille
"""
import os
import struct
import sys
import zlib

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TABLE_CAPACITY = 300
ENTRY_SIZE = 20


class FormatError(Exception):
    pass


# --- décompression PKWARE DCL « implode » (algorithme de blast.c, Mark Adler) -------------

def _construct(rep):
    """Table de Huffman canonique à partir de la forme compacte (longueur, répétitions)."""
    lengths = []
    for b in rep:
        lengths += [b & 15] * ((b >> 4) + 1)
    count = [0] * 14
    for n in lengths:
        count[n] += 1
    offs = [0] * 14
    for n in range(1, 13):
        offs[n + 1] = offs[n] + count[n]
    symbol = [0] * len(lengths)
    for sym, n in enumerate(lengths):
        if n:
            symbol[offs[n]] = sym
            offs[n] += 1
    return count, symbol


_LENCODE = _construct([2, 35, 36, 53, 38, 23])
_DISTCODE = _construct([2, 20, 53, 230, 247, 151, 248])
_BASE = [3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264]
_EXTRA = [0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8]


def explode(data, expected=None):
    """Décompresse un flux implode en mode binaire. Lève FormatError si le flux est invalide."""
    if len(data) < 2 or data[0] != 0 or not 4 <= data[1] <= 6:
        raise FormatError(f"en-tête implode inattendu : {data[:2].hex()}")
    dict_bits = data[1]
    pos, bitbuf, bitcnt = 2, 0, 0
    n = len(data)
    out = bytearray()

    def bits(need):
        nonlocal pos, bitbuf, bitcnt
        while bitcnt < need:
            if pos >= n:
                raise FormatError("flux implode tronqué")
            bitbuf |= data[pos] << bitcnt
            pos += 1
            bitcnt += 8
        val = bitbuf & ((1 << need) - 1)
        bitbuf >>= need
        bitcnt -= need
        return val

    def decode(table):
        count, symbol = table
        code = first = index = 0
        for length in range(1, 14):
            code |= bits(1) ^ 1  # les codes sont stockés inversés
            c = count[length]
            if code < first + c:
                return symbol[index + (code - first)]
            index += c
            first = (first + c) << 1
            code <<= 1
        raise FormatError("code de Huffman invalide")

    while True:
        if bits(1):
            sym = decode(_LENCODE)
            length = _BASE[sym] + bits(_EXTRA[sym])
            if length == 519:
                break
            low = 2 if length == 2 else dict_bits
            dist = (decode(_DISTCODE) << low) + bits(low) + 1
            if dist > len(out):
                raise FormatError("distance hors du texte déjà décodé")
            start = len(out) - dist
            for k in range(length):  # recouvrement possible : copie octet par octet
                out.append(out[start + k])
        else:
            out.append(bits(8))
        if expected is not None and len(out) > expected:
            raise FormatError("bloc plus long qu'annoncé")
    return bytes(out), pos


# --- conteneur --------------------------------------------------------------------------

class Chunk:
    __slots__ = ("index", "compressed", "address", "element_size", "count", "extra", "offset",
                 "stored_size", "data")

    @property
    def size(self):
        return self.element_size * self.count


class MapFile:
    """Suite de blocs ; la table (elle-même un bloc) donne pour chacun l'adresse de la variable
    du jeu, la taille d'un élément, le nombre d'éléments et un indicateur de compression."""

    def __init__(self, path, start=0):
        with open(path, "rb") as fh:
            d = fh.read()
        self.path = path
        self.start = start
        # Une carte commence par un bloc « MAPS » ; une sauvegarde n'a pas ce bloc.
        self.has_magic = d[start:start + 4] == b"MAPS"
        lead = 4 if self.has_magic else 0
        self.version, self.num_chunks = struct.unpack_from("<II", d, start + lead)
        if not 4 <= self.num_chunks <= TABLE_CAPACITY:
            raise FormatError(f"nombre de blocs invraisemblable : {self.num_chunks}")
        table = start + lead + 8
        self.chunks = []
        for i in range(self.num_chunks):
            c = Chunk()
            c.index = i
            c.compressed, c.address, c.element_size, c.count, c.extra = \
                struct.unpack_from("<5I", d, table + ENTRY_SIZE * i)
            self.chunks.append(c)
        # Premiers blocs : (signature,) version, nombre de blocs, puis la table elle-même.
        n_head = 4 if self.has_magic else 3
        head = [(c.size, c.compressed) for c in self.chunks[:n_head]]
        if head != [(4, 0)] * (n_head - 1) + [(TABLE_CAPACITY * ENTRY_SIZE, 0)]:
            raise FormatError(f"en-tête de table inattendu : {head}")
        pos = start
        for c in self.chunks:
            c.offset = pos
            if c.compressed == 0:
                c.data = d[pos:pos + c.size]
                c.stored_size = c.size
            elif c.compressed == 1:
                length, = struct.unpack_from("<I", d, pos)
                if length == 0x80000000:  # bloc marqué compressible mais stocké tel quel
                    c.data = d[pos + 4:pos + 4 + c.size]
                    c.stored_size = 4 + c.size
                else:
                    if pos + 4 + length > len(d):
                        raise FormatError(f"bloc {c.index} : longueur {length} hors fichier")
                    c.data, used = explode(d[pos + 4:pos + 4 + length], c.size)
                    c.stored_size = 4 + length
            else:
                raise FormatError(f"bloc {c.index} : indicateur de compression {c.compressed}")
            if len(c.data) != c.size:
                raise FormatError(f"bloc {c.index} : {len(c.data)} octets au lieu de {c.size}")
            pos += c.stored_size
        self.end = pos
        self.file_size = len(d)

    def by_address(self, address):
        for c in self.chunks:
            if c.address == address:
                return c
        raise KeyError(hex(address))


def find_containers(path):
    """Offsets des conteneurs MAPS d'un fichier (un .pak en renferme plusieurs)."""
    with open(path, "rb") as fh:
        d = fh.read()
    offsets, pos = [], d.find(b"MAPS")
    while pos >= 0:
        offsets.append(pos)
        pos = d.find(b"MAPS", pos + 4)
    return offsets


def write_png_gray(path, values, w, h, maximum):
    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))
    scale = 255 / maximum if maximum else 0
    rows = b"".join(b"\0" + bytes(min(255, int(v * scale)) for v in values[y * w:(y + 1) * w])
                    for y in range(h))
    with open(path, "wb") as fh:
        fh.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
                 + chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))


def project_dir(path):
    path = os.path.abspath(path)
    if os.path.commonpath([path, PROJECT_ROOT]) != PROJECT_ROOT or path == PROJECT_ROOT:
        sys.exit(f"refus : {path} n'est pas dans le projet")
    if os.path.exists(path) and os.listdir(path):
        sys.exit(f"refus : {path} existe et n'est pas vide")
    os.makedirs(path, exist_ok=True)
    return path


def cmd_info(path):
    mf = MapFile(path)
    print(f"{os.path.basename(path)} : version {mf.version}, {mf.num_chunks} blocs, "
          f"lu jusqu'à {mf.end} sur {mf.file_size} octets")
    for c in mf.chunks:
        zeros = c.data.count(0) * 100 // max(len(c.data), 1)
        print(f"  {c.index:3} adr={c.address:#010x} {c.element_size:>6} x {c.count:<6} = {c.size:>7} o"
              f"  {'comp' if c.compressed else 'brut'} stocké={c.stored_size:>7}  extra={c.extra:<9}"
              f" zéros={zeros:3}%")


def cmd_check(root):
    results = {}
    for folder, _, names in sorted(os.walk(root)):
        for name in sorted(names):
            ext = os.path.splitext(name)[1].lower()
            if ext not in (".map", ".sav", ".pak", ".set"):
                continue
            path = os.path.join(folder, name)
            ok = bad = 0
            first = ""
            try:
                mf = MapFile(path)
                exact = mf.end == mf.file_size
                ok = 1
                first = "" if exact else f"reste {mf.file_size - mf.end} octets après le dernier bloc"
            except (FormatError, struct.error, IndexError) as exc:
                bad = 1
                first = f"{type(exc).__name__}: {exc}"
            r = results.setdefault(ext, [0, 0, 0, []])
            r[0] += ok
            r[1] += bad
            if first:
                r[2] += 1
                if len(r[3]) < 3:
                    r[3].append(f"{name} : {first}")
    for ext, (ok, bad, notes, examples) in sorted(results.items()):
        print(f"{ext} : {ok} lus, {bad} en échec, {notes} avec remarque")
        for e in examples:
            print("    " + e)


def cmd_dump(path, dest):
    dest = project_dir(dest)
    mf = MapFile(path)
    for c in mf.chunks:
        with open(os.path.join(dest, f"{c.index:03d}_{c.address:08x}_{c.element_size}x{c.count}.bin"),
                  "xb") as fh:
            fh.write(c.data)
    print(f"{mf.num_chunks} blocs écrits dans {dest}")


def cmd_grids(path, dest):
    dest = project_dir(dest)
    mf = MapFile(path)
    sizes = {}
    for c in mf.chunks:
        if c.element_size in (1, 2, 4) and c.count > 1024:
            sizes[c.count] = sizes.get(c.count, 0) + 1
    cells = max(sizes, key=sizes.get)
    side = int(round(cells ** 0.5))
    if side * side != cells:
        sys.exit(f"grille non carrée : {cells} cases")
    fmt = {1: "B", 2: "H", 4: "I"}
    count = 0
    for c in mf.chunks:
        if c.count != cells or c.element_size not in fmt:
            continue
        values = struct.unpack(f"<{cells}{fmt[c.element_size]}", c.data)
        distinct = len(set(values))
        write_png_gray(os.path.join(dest, f"{c.index:03d}_{c.address:08x}_u{c.element_size * 8}.png"),
                       values, side, side, max(values))
        print(f"  bloc {c.index:3} adr={c.address:#010x} u{c.element_size * 8:<2} max={max(values):<10} "
              f"valeurs distinctes={distinct}")
        count += 1
    print(f"{count} grilles de {side}x{side} écrites dans {dest}")


def main(argv):
    if len(argv) == 3 and argv[1] == "info":
        cmd_info(argv[2])
    elif len(argv) == 3 and argv[1] == "check":
        cmd_check(argv[2])
    elif len(argv) == 4 and argv[1] == "dump":
        cmd_dump(argv[2], argv[3])
    elif len(argv) == 4 and argv[1] == "grids":
        cmd_grids(argv[2], argv[3])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
