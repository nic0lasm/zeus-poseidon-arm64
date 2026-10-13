#!/usr/bin/env python3
"""Lecture des archives InstallShield 5 et 6 (dataN.cab / dataN.hdr, signature « ISc( »).

Usage :
    ishield.py list    DATA1.CAB
    ishield.py extract DATA1.CAB DEST     (DEST : dossier vide ou absent, dans le projet)

Les fichiers InstallShield 6 portent un MD5 : il est vérifié à l'extraction.
Structures reprises de la documentation du projet libre « unshield ».
"""
import hashlib
import os
import re
import struct
import sys
import zlib

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIGNATURE = 0x28635349
FILE_SPLIT, FILE_OBFUSCATED, FILE_COMPRESSED, FILE_INVALID = 1, 2, 4, 8


class FileEntry:
    __slots__ = ("index", "name", "directory", "flags", "expanded_size", "compressed_size",
                 "data_offset", "md5", "volume", "link_previous", "link_next", "link_flags")

    @property
    def path(self):
        return (self.directory + "\\" + self.name) if self.directory else self.name


class Cabinet:
    def __init__(self, cab_path):
        self.dir = os.path.dirname(os.path.abspath(cab_path))
        m = re.fullmatch(r"(.*?)1\.(cab|hdr)", os.path.basename(cab_path), re.I)
        if not m:
            raise ValueError("donner le premier volume (data1.cab)")
        self.base = m.group(1)
        self.names = {n.lower(): n for n in os.listdir(self.dir)}
        hdr = self._find(f"{self.base}1.hdr") or self._find(f"{self.base}1.cab")
        with open(hdr, "rb") as fh:
            sig, version, _, desc_off, _ = struct.unpack("<5I", fh.read(20))
            if sig != SIGNATURE:
                raise ValueError("signature ISc( absente")
            fh.seek(desc_off)
            c = self.c = fh.read() if hdr.lower().endswith(".hdr") else fh.read(8 * 1024 * 1024)
        self.version = version
        if version >> 24 == 1:
            self.major = (version >> 12) & 0xF
        else:
            self.major = (version & 0xFFFF) // 100
        if self.major not in (0, 5, 6):
            raise ValueError(f"version InstallShield non gérée : {self.major} ({version:#x})")
        self.fto, = struct.unpack_from("<I", c, 0x0C)
        self.dir_count, = struct.unpack_from("<I", c, 0x1C)
        self.file_count, self.fto2 = struct.unpack_from("<2I", c, 0x28)
        self.volume_headers = {}
        count = self.dir_count + (self.file_count if self.major < 6 else 0)
        self.table = struct.unpack_from(f"<{count}I", c, self.fto)
        self.directories = [self._cstr(self.fto + self.table[i]) for i in range(self.dir_count)]
        self.files = [self._descriptor(i) for i in range(self.file_count)]

    def _find(self, name):
        real = self.names.get(name.lower())
        return os.path.join(self.dir, real) if real else None

    def _cstr(self, offset):
        return self.c[offset:self.c.index(b"\0", offset)].decode("cp1252")

    def _descriptor(self, i):
        c, e = self.c, FileEntry()
        e.index = i
        e.md5 = None
        e.link_previous = e.link_next = e.link_flags = 0
        if self.major < 6:
            p = self.fto + self.table[self.dir_count + i]
            name_off, dir_index, e.flags, e.expanded_size, e.compressed_size = \
                struct.unpack_from("<IIHII", c, p)
            e.data_offset, = struct.unpack_from("<I", c, p + 38)
            if self.major == 5:
                e.md5 = c[p + 42:p + 58]
            e.volume = 1
        else:
            p = self.fto + self.fto2 + i * 0x57
            e.flags, e.expanded_size, _, e.compressed_size, _, e.data_offset, _ = \
                struct.unpack_from("<H6I", c, p)
            e.md5 = c[p + 0x1A:p + 0x2A]
            name_off, dir_index = struct.unpack_from("<IH", c, p + 0x3A)
            e.link_previous, e.link_next, e.link_flags, e.volume = \
                struct.unpack_from("<IIBH", c, p + 0x4C)
        try:
            e.name = self._cstr(self.fto + name_off)
            e.directory = self.directories[dir_index]
        except (ValueError, IndexError):
            # entrées FILE_INVALID : les champs ne sont pas renseignés
            e.name, e.directory = f"?invalide-{i}", ""
            e.flags |= FILE_INVALID
        return e

    def volume_header(self, n):
        if n not in self.volume_headers:
            path = self._find(f"{self.base}{n}.cab")
            if path is None:
                raise FileNotFoundError(f"volume {self.base}{n}.cab introuvable")
            with open(path, "rb") as fh:
                d = fh.read(0x14 + 16 * 4)
            if struct.unpack_from("<I", d, 0)[0] != SIGNATURE:
                raise ValueError(f"{path} : signature absente")
            if self.major < 6:
                keys = ("data_offset", "first_index", "last_index", "first_offset", "first_expanded",
                        "first_compressed", "last_offset", "last_expanded", "last_compressed")
                h = dict(zip(keys, struct.unpack_from("<9I", d, 0x14)))
            else:
                # champs 64 bits : seule la moitié basse est utilisée
                v = struct.unpack_from("<16I", d, 0x14)
                h = dict(data_offset=v[0], first_index=v[2], last_index=v[3], first_offset=v[4],
                         first_expanded=v[6], first_compressed=v[8], last_offset=v[10],
                         last_expanded=v[12], last_compressed=v[14])
            h["path"] = path
            self.volume_headers[n] = h
        return self.volume_headers[n]

    def _raw_stream(self, e):
        """Octets stockés du fichier, en suivant les volumes si le fichier est fractionné."""
        compressed = bool(e.flags & FILE_COMPRESSED)
        total = e.compressed_size if compressed else e.expanded_size
        volume, seed = e.volume, 0
        while total > 0:
            h = self.volume_header(volume)
            if e.flags & FILE_SPLIT:
                if e.index == h["last_index"] and h["last_offset"] not in (0, 0x7FFFFFFF):
                    offset = h["last_offset"]
                    part = h["last_compressed"] if compressed else h["last_expanded"]
                elif e.index == h["first_index"]:
                    offset = h["first_offset"]
                    part = h["first_compressed"] if compressed else h["first_expanded"]
                else:
                    raise ValueError(f"{e.path} : fragment introuvable dans le volume {volume}")
            else:
                offset, part = e.data_offset, total
            part = min(part, total)
            with open(h["path"], "rb") as fh:
                fh.seek(offset)
                left = part
                while left > 0:
                    buf = fh.read(min(left, 1 << 20))
                    if not buf:
                        raise ValueError(f"{e.path} : fin de volume prématurée")
                    left -= len(buf)
                    if e.flags & FILE_OBFUSCATED:
                        out = bytearray(len(buf))
                        for i, b in enumerate(buf):
                            b ^= 0xD5
                            out[i] = (((b >> 2) | (b << 6)) - (seed % 0x47)) & 0xFF
                            seed += 1
                        buf = bytes(out)
                    yield buf
            total -= part
            volume += 1

    def read(self, e):
        """Contenu décompressé d'un fichier (bytes)."""
        raw = b"".join(self._raw_stream(e))
        if not e.flags & FILE_COMPRESSED:
            return raw
        out, pos = [], 0
        while pos < len(raw):
            n, = struct.unpack_from("<H", raw, pos)
            pos += 2
            out.append(zlib.decompressobj(-15).decompress(raw[pos:pos + n]))
            pos += n
        return b"".join(out)

    def has_data(self, e):
        return not e.flags & FILE_INVALID and (e.data_offset != 0 or e.flags & FILE_SPLIT)


def safe_target(dest, entry):
    parts = [p for p in entry.path.replace("/", "\\").split("\\")]
    if any(p in ("", ".", "..") or ":" in p or "\x00" in p for p in parts):
        return None
    return os.path.join(dest, *parts)


def cmd_list(path):
    cab = Cabinet(path)
    print(f"# InstallShield {cab.major or 5} (version {cab.version:#010x}), "
          f"{cab.file_count} fichiers, {cab.dir_count} dossiers")
    for e in cab.files:
        md5 = e.md5.hex() if e.md5 else "-"
        print(f"{e.expanded_size:>10} {e.compressed_size:>10} f={e.flags:<2} v={e.volume} "
              f"@{e.data_offset:<9x} {md5}  {e.path}")


def cmd_extract(path, dest):
    dest = os.path.abspath(dest)
    if os.path.commonpath([dest, PROJECT_ROOT]) != PROJECT_ROOT or dest == PROJECT_ROOT:
        sys.exit(f"refus : {dest} n'est pas un sous-dossier du projet")
    if os.path.exists(dest) and os.listdir(dest):
        sys.exit(f"refus : {dest} existe et n'est pas vide")
    cab = Cabinet(path)
    os.makedirs(dest, exist_ok=True)
    done = skipped = verified = 0
    problems = []
    for e in cab.files:
        target = safe_target(dest, e)
        if target is None or not cab.has_data(e):
            skipped += 1
            problems.append(f"ignoré (hors archive) : {e.path} flags={e.flags}")
            continue
        try:
            data = cab.read(e)
        except Exception as exc:  # consigné, l'extraction continue pour mesurer l'étendue
            problems.append(f"ECHEC {e.path} : {exc}")
            continue
        if len(data) != e.expanded_size:
            problems.append(f"ECHEC {e.path} : taille {len(data)} != {e.expanded_size}")
            continue
        if e.md5 and any(e.md5):
            if hashlib.md5(data).digest() != e.md5:
                problems.append(f"ECHEC {e.path} : MD5 incorrect")
                continue
            verified += 1
        os.makedirs(os.path.dirname(target), exist_ok=True)
        if os.path.exists(target):
            problems.append(f"doublon : {e.path}")
            continue
        with open(target, "xb") as fh:
            fh.write(data)
        done += 1
    print(f"{done} fichiers extraits ({verified} vérifiés par MD5), {skipped} ignorés, "
          f"{len(problems) - skipped} problèmes")
    for p in problems:
        print("  " + p)


def main(argv):
    if len(argv) == 3 and argv[1] == "list":
        cmd_list(argv[2])
    elif len(argv) == 4 and argv[1] == "extract":
        cmd_extract(argv[2], argv[3])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
