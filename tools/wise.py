#!/usr/bin/env python3
"""Lecture des installateurs Wise (mises à jour Zeus 1.1 et Poséidon 2.1), sans les exécuter.

Après l'exécutable d'amorce, un installateur Wise est une suite de flux deflate bruts, chacun
suivi du CRC-32 de son contenu décompressé. Le flux n°1 est le script d'installation : il donne,
pour chaque flux, le chemin de destination (repéré ici par le CRC-32 du flux).

Usage :
    wise.py list    INSTALLER.EXE
    wise.py extract INSTALLER.EXE DEST    (DEST : dossier vide ou absent, dans le projet)
"""
import os
import re
import struct
import sys
import zlib

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def pe_end(d):
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    count = struct.unpack_from("<H", d, pe + 6)[0]
    table = pe + 24 + struct.unpack_from("<H", d, pe + 20)[0]
    return max(sum(struct.unpack_from("<2I", d, table + 40 * i + 16)[::-1][:2])
               for i in range(count))


def inflate_at(d, pos):
    """(contenu, fin) du flux deflate brut commençant à pos, ou None."""
    o = zlib.decompressobj(-15)
    try:
        out = o.decompress(d[pos:])
    except zlib.error:
        return None
    if not o.eof:
        return None
    return out, len(d) - len(o.unused_data)


def streams(d):
    """Liste de (début, fin, contenu) ; lève une erreur si l'installateur n'est pas couvert."""
    start = pe_end(d)
    pos = start
    while pos < start + 0x400:  # l'en-tête Wise précède le premier flux
        r = inflate_at(d[:pos + (1 << 20)], pos)
        if r and len(r[0]) > 16 and struct.unpack_from("<I", d, r[1])[0] == zlib.crc32(r[0]):
            break
        pos += 1
    else:
        raise ValueError("aucun flux Wise trouvé")
    found = []
    while pos < len(d):
        r = inflate_at(d, pos)
        if r is None:
            raise ValueError(f"flux illisible à {pos:#x}")
        out, end = r
        if struct.unpack_from("<I", d, end)[0] != zlib.crc32(out):
            raise ValueError(f"CRC-32 incorrect pour le flux à {pos:#x}")
        found.append((pos, end, out))
        pos = end + 4
    return found


def destinations(script):
    """CRC-32 -> chemins de destination lus dans le script (chaînes %VAR%\\... qui suivent le CRC)."""
    names = {}
    for m in re.finditer(rb"(?s)(.{4})(%[A-Z0-9_]+%[^\x00]{1,200})\x00", script):
        crc = struct.unpack("<I", m.group(1))[0]
        names.setdefault(crc, []).append(m.group(2).decode("cp1252"))
    return names


def describe(d):
    found = streams(d)
    names = destinations(found[1][2])
    rows = []
    for i, (start, end, out) in enumerate(found):
        dest = names.get(zlib.crc32(out), [])
        kind = "script" if i == 1 else ("correctif binaire" if out[:1] == b"\x01" and dest else "")
        rows.append((i, start, end - start, out, dest, kind))
    return rows


def cmd_list(path):
    d = open(path, "rb").read()
    for i, start, comp, out, dest, kind in describe(d):
        print(f"#{i:<3} @{start:#9x} comp={comp:>8} taille={len(out):>8} crc={zlib.crc32(out):08x} "
              f"{kind:18} {' | '.join(dest)}")


def cmd_extract(path, dest_dir):
    dest_dir = os.path.abspath(dest_dir)
    if os.path.commonpath([dest_dir, PROJECT_ROOT]) != PROJECT_ROOT or dest_dir == PROJECT_ROOT:
        sys.exit(f"refus : {dest_dir} n'est pas un sous-dossier du projet")
    if os.path.exists(dest_dir) and os.listdir(dest_dir):
        sys.exit(f"refus : {dest_dir} existe et n'est pas vide")
    os.makedirs(dest_dir, exist_ok=True)
    d = open(path, "rb").read()
    with open(os.path.join(dest_dir, "index.tsv"), "x", encoding="utf-8") as index:
        index.write("fichier\ttaille\tcrc32\tdestination\n")
        for i, start, comp, out, dest, kind in describe(d):
            base = re.sub(r"[^A-Za-z0-9._ -]", "_", dest[0].split("\\")[-1]) if dest else "sans-nom.bin"
            name = f"{i:03d}_{'script.bin' if i == 1 else base}"
            with open(os.path.join(dest_dir, name), "xb") as fh:
                fh.write(out)
            index.write(f"{name}\t{len(out)}\t{zlib.crc32(out):08x}\t{' | '.join(dest)}\n")
    print(f"flux extraits vers {dest_dir}")


def main(argv):
    if len(argv) == 3 and argv[1] == "list":
        cmd_list(argv[2])
    elif len(argv) == 4 and argv[1] == "extract":
        cmd_extract(argv[2], argv[3])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
