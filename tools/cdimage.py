#!/usr/bin/env python3
"""Lecture d'images CD (brutes 2352 octets/secteur ou ISO 2048) et de leur système ISO 9660.

Usage :
    cdimage.py info    IMAGE
    cdimage.py list    IMAGE
    cdimage.py extract IMAGE DEST     (DEST doit être un dossier vide ou absent, dans le projet)
"""
import os
import struct
import sys

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SYNC = b"\x00" + b"\xff" * 10 + b"\x00"


class CdImage:
    """Accès aux secteurs logiques de 2048 octets, quel que soit le format de l'image."""

    def __init__(self, path):
        self.path = path
        self.f = open(path, "rb")
        self.size = os.path.getsize(path)
        head = self.f.read(16)
        if head[:12] == SYNC:
            self.raw = 2352
            self.mode = head[15]
            if self.mode not in (1, 2):
                raise ValueError(f"mode de secteur inattendu : {self.mode}")
            self.data_offset = 16 if self.mode == 1 else 24
        else:
            self.raw, self.mode, self.data_offset = 2048, 0, 0
        if self.size % self.raw:
            raise ValueError(f"taille {self.size} non multiple de {self.raw}")
        self.sector_count = self.size // self.raw
        self.form2_sectors = 0

    def read_sector(self, lba):
        if not 0 <= lba < self.sector_count:
            raise ValueError(f"secteur {lba} hors image ({self.sector_count})")
        self.f.seek(lba * self.raw)
        raw = self.f.read(self.raw)
        if self.raw == 2352:
            if raw[:12] != SYNC:
                raise ValueError(f"secteur {lba} : synchro absente")
            if self.mode == 2 and raw[18] & 0x20:
                self.form2_sectors += 1
        return raw[self.data_offset:self.data_offset + 2048]

    def read_extent(self, lba, length):
        out = bytearray()
        for i in range((length + 2047) // 2048):
            out += self.read_sector(lba + i)
        return bytes(out[:length])

    def copy_extent(self, lba, length, fh):
        remaining = length
        while remaining > 0:
            data = self.read_sector(lba)
            fh.write(data[:min(2048, remaining)])
            remaining -= 2048
            lba += 1


class Entry:
    __slots__ = ("path", "is_dir", "extents", "size", "date")

    def __init__(self, path, is_dir, extents, size, date):
        self.path, self.is_dir, self.extents, self.size, self.date = path, is_dir, extents, size, date


class Iso9660:
    def __init__(self, image):
        self.img = image
        self.pvd = None
        self.joliet = None
        lba = 16
        while True:
            d = image.read_sector(lba)
            if d[1:6] != b"CD001":
                raise ValueError(f"descripteur de volume invalide au secteur {lba}")
            kind = d[0]
            if kind == 1:
                self.pvd = d
            elif kind == 2 and d[88:91] in (b"%/@", b"%/C", b"%/E"):
                self.joliet = d
            elif kind == 255:
                break
            lba += 1
        if self.pvd is None:
            raise ValueError("pas de descripteur de volume primaire")
        self.volume_id = self.pvd[40:72].decode("ascii", "replace").strip()
        self.volume_sectors = struct.unpack_from("<I", self.pvd, 80)[0]

    def _records(self, lba, length, joliet):
        data = self.img.read_extent(lba, length)
        pos = 0
        while pos < len(data):
            rec_len = data[pos]
            if rec_len == 0:  # fin des enregistrements de ce secteur
                pos = (pos // 2048 + 1) * 2048
                continue
            rec = data[pos:pos + rec_len]
            pos += rec_len
            extent, size = struct.unpack_from("<I4xI", rec, 2)
            y, mo, d, h, mi, s = rec[18:24]
            flags = rec[25]
            raw_name = rec[33:33 + rec[32]]
            if raw_name in (b"\x00", b"\x01"):
                continue
            if joliet:
                name = raw_name.decode("utf-16-be")
            else:
                name = raw_name.decode("ascii", "replace")
            name = name.split(";")[0]
            if name.endswith(".") and not flags & 2:
                name = name[:-1]
            date = f"{1900 + y:04d}-{mo:02d}-{d:02d} {h:02d}:{mi:02d}:{s:02d}"
            yield name, flags, extent + rec[1], size, date

    def walk(self, joliet=None):
        """Parcourt l'arborescence ; Joliet (noms longs) par défaut s'il existe."""
        if joliet is None:
            joliet = self.joliet is not None
        vd = self.joliet if joliet else self.pvd
        root_lba, root_len = struct.unpack_from("<I4xI", vd, 156 + 2)
        stack = [("", root_lba, root_len)]
        seen = set()
        while stack:
            prefix, lba, length = stack.pop()
            if lba in seen:
                raise ValueError(f"boucle de répertoires au secteur {lba}")
            seen.add(lba)
            pending = None  # fichier multi-extents en cours
            for name, flags, extent, size, date in self._records(lba, length, joliet):
                path = prefix + name
                if flags & 2:
                    yield Entry(path, True, [], 0, date)
                    stack.append((path + "/", extent, size))
                    continue
                if pending is None:
                    pending = Entry(path, False, [], 0, date)
                pending.extents.append((extent, size))
                pending.size += size
                if not flags & 0x80:
                    yield pending
                    pending = None


def safe_target(dest, rel):
    parts = rel.split("/")
    if any(p in ("", ".", "..") or "\\" in p or "\x00" in p for p in parts):
        raise ValueError(f"nom dangereux dans l'image : {rel!r}")
    return os.path.join(dest, *parts)


def cmd_info(path):
    img = CdImage(path)
    iso = Iso9660(img)
    entries = list(iso.walk())
    files = [e for e in entries if not e.is_dir]
    print(f"image          : {os.path.basename(path)} ({img.size} octets)")
    print(f"secteurs       : {img.sector_count} x {img.raw} octets, mode {img.mode}")
    print(f"volume         : {iso.volume_id!r}, {iso.volume_sectors} secteurs déclarés")
    print(f"Joliet         : {'oui' if iso.joliet else 'non'}")
    print(f"contenu        : {len(files)} fichiers, {len(entries) - len(files)} dossiers, "
          f"{sum(e.size for e in files)} octets")
    last = max((lba + (size + 2047) // 2048 for e in files for lba, size in e.extents), default=0)
    print(f"dernier secteur de fichier : {last} (image : {img.sector_count})")


def cmd_list(path):
    iso = Iso9660(CdImage(path))
    for e in sorted(iso.walk(), key=lambda e: e.path.lower()):
        print(f"{'<DIR>' if e.is_dir else e.size:>11}  {e.date}  {e.path}{'/' if e.is_dir else ''}")


def cmd_extract(path, dest):
    dest = os.path.abspath(dest)
    if os.path.commonpath([dest, PROJECT_ROOT]) != PROJECT_ROOT or dest == PROJECT_ROOT:
        sys.exit(f"refus : {dest} n'est pas un sous-dossier du projet")
    if os.path.exists(dest) and os.listdir(dest):
        sys.exit(f"refus : {dest} existe et n'est pas vide")
    img = CdImage(path)
    iso = Iso9660(img)
    os.makedirs(dest, exist_ok=True)
    count = total = 0
    for e in sorted(iso.walk(), key=lambda e: e.path):
        target = safe_target(dest, e.path)
        if e.is_dir:
            os.makedirs(target, exist_ok=True)
            continue
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "xb") as fh:
            for lba, size in e.extents:
                img.copy_extent(lba, size, fh)
        if os.path.getsize(target) != e.size:
            sys.exit(f"taille incorrecte pour {e.path}")
        count += 1
        total += e.size
    print(f"{count} fichiers extraits, {total} octets, vers {dest}")
    if img.form2_sectors:
        print(f"attention : {img.form2_sectors} secteurs Mode 2 Form 2 rencontrés (données XA)")


def main(argv):
    if len(argv) == 3 and argv[1] == "info":
        cmd_info(argv[2])
    elif len(argv) == 3 and argv[1] == "list":
        cmd_list(argv[2])
    elif len(argv) == 4 and argv[1] == "extract":
        cmd_extract(argv[2], argv[3])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
