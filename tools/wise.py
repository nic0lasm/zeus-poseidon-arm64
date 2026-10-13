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


class PatchError(Exception):
    pass


def _varint(src, pos, value, steps=3):
    """Suite d'un entier à longueur variable : jusqu'à 2 octets de 7 bits puis 1 octet de 8 bits.

    pos désigne le dernier octet déjà lu ; chaque octet lu dont le bit 7 est à 1 annonce le suivant.
    """
    for step in range(steps):
        if not src[pos] & 0x80 and step:
            break
        pos += 1
        value = (value << 8 | src[pos]) if step == steps - 1 else (value << 7 | src[pos] & 0x7F)
        if step == steps - 1:
            break
    return value, pos


def _delta(src, pos, old, limit):
    """Applique un flux de commandes Wise (lu dans src à partir de pos) à old.

    Chaque commande insère des octets littéraux puis copie une plage de l'ancien fichier.
    Format établi d'après la bibliothèque de correctifs de l'installateur (fonction 0x100017a1).
    """
    cmd_len, = struct.unpack_from("<I", src, pos)
    cmd = pos + 4
    cmd_end = lit = cmd + cmd_len
    out = bytearray()
    old_pos = 0
    while len(out) < limit and cmd < cmd_end:
        b = src[cmd]
        offset = 0
        if not b & 0x80:  # forme courte : 4 bits de littéraux, 4 bits de copie
            n_copy, n_lit = b & 0x0F, b >> 4
            cmd += 1
        else:
            n_lit, p = b & 0x3F, cmd
            if b & 0x40:
                n_lit, p = _varint(src, cmd, n_lit)
            b2 = src[p + 1]
            n_copy, q = b2 & 0x3F, p + 1
            if b2 & 0x80:
                n_copy, q = _varint(src, p + 1, n_copy)
            cmd = q + 1
            if not b2 & 0x40:  # déplacement relatif signé dans l'ancien fichier
                c = src[cmd]
                offset = c & 0x7F
                if offset > 0x3F:
                    offset -= 0x80
                if c & 0x80:
                    offset, cmd = _varint(src, cmd, offset)
                cmd += 1
        n_copy += 3
        if n_lit:
            if lit + n_lit > len(src):
                raise PatchError("littéraux manquants")
            out += src[lit:lit + n_lit]
            lit += n_lit
            old_pos += n_lit
        if len(out) < limit:
            old_pos += offset
            if old_pos < 0 or old_pos + n_copy > len(old):
                raise PatchError(f"copie hors de l'ancien fichier ({old_pos}+{n_copy})")
            out += old[old_pos:old_pos + n_copy]
            old_pos += n_copy
    return bytes(out)


def patch_info(patch):
    """En-tête d'un correctif : (crc, taille) du nouveau fichier et [(offset, crc, taille)] des anciens."""
    count = patch[0]
    new_crc, new_size = struct.unpack_from("<II", patch, 5)
    versions = [struct.unpack_from("<III", patch, 13 + 12 * k) for k in range(count)]
    return (new_crc, new_size), versions


def is_patch_for(patch, old):
    """Vrai si patch est un correctif Wise dont old est une version connue (ancienne ou nouvelle)."""
    if len(patch) < 25 or not 1 <= patch[0] <= 32 or len(patch) < 13 + 12 * patch[0]:
        return False
    new, versions = patch_info(patch)
    me = (zlib.crc32(old), len(old))
    return me == new or any(me == (crc, size) for _, crc, size in versions)


def apply_patch(patch, old):
    """Nouveau fichier obtenu en appliquant patch à old ; CRC-32 et taille vérifiés."""
    (new_crc, new_size), versions = patch_info(patch)
    me = (zlib.crc32(old), len(old))
    if me == (new_crc, new_size):
        return old
    for k, (offset, crc, size) in enumerate(versions):
        if me == (crc, size):
            break
    else:
        raise PatchError("ce fichier n'est pas une version connue du correctif")
    if versions[0][0] == 0:  # le correctif contient le fichier complet
        new = patch[13 + 12 * len(versions):]
    elif k == 0:
        new = _delta(patch, offset, old, new_size)
    else:  # version plus ancienne : son flux se déduit d'abord du correctif lui-même
        new = _delta(_delta(patch, offset, patch, 0x7FFFFFFF), 0, old, new_size)
    if len(new) != new_size or zlib.crc32(new) != new_crc:
        raise PatchError("résultat incorrect (taille ou CRC-32)")
    return new


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
