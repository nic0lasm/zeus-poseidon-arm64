#!/usr/bin/env python3
"""Lecture et réécriture à l'octet près des aventures publiées (`.pak`, voir formats/pak.md).

Usage :
    pakfile.py info  FICHIER            en-tête et cartes incluses
    pakfile.py check CHEMIN...          relit chaque .pak (fichiers ou dossiers), le réécrit et
                                        compare à l'original, octet par octet

Un .pak est l'en-tête d'un .set (setfile.py) suivi des cartes de l'aventure, conteneurs MAPS
recopiés bout à bout (mapfile.py) ; chaque carte est recompressée avec sa propre structure de
travail, comme le fichier .map qu'elle copie.
"""
import multiprocessing
import os
import sys
import time

import mapfile
import setfile

MAP_CODES = ["P", "C1", "C2", "C3", "C4"]  # codes 0, 10 à 13 de 0x0042A450


class PakFile:
    def __init__(self, path):
        with open(path, "rb") as fh:
            self.data = fh.read()
        self.header = setfile.SetHeader(self.data)
        self.maps = []
        pos = self.header.end
        while pos < len(self.data):
            m = mapfile.MapFile(path, pos)
            self.maps.append(m)
            pos = m.end
        if not self.maps:
            raise setfile.FormatError("aucune carte après l'en-tête")

    def to_bytes(self):
        return self.header.to_bytes() + b"".join(m.to_bytes() for m in self.maps)

    def consistency(self):
        """Écarts avec ce que fait l'écriture du jeu (0x0042A450) : décalages des cartes et
        tampon de copie qui commence par la dernière carte."""
        notes = []
        offsets = self.header.ints("map_offsets")
        starts = [m.start for m in self.maps]
        if offsets[:len(starts)] != starts:
            notes.append(f"décalages {offsets} au lieu de {starts}")
        last = self.data[self.maps[-1].start:self.maps[-1].end]
        if not self.header.values["map_buffer"].startswith(last):
            notes.append("le tampon de copie ne commence pas par la dernière carte")
        return notes


def cmd_info(path):
    p = PakFile(path)
    h = p.header
    print(f"{path} : version {h.version}, genre {h.values['kind'][0]}, en-tête de {h.end} octets, "
          f"{len(p.maps)} carte(s)")
    for i, m in enumerate(p.maps):
        print(f"  carte {i} à {m.start:8d} : {m.end - m.start:7d} octets, version {m.version}, "
              f"{m.num_chunks} blocs")
    print(f"  décalages de l'en-tête : {h.ints('map_offsets')}")
    for note in p.consistency():
        print("  remarque : " + note)


def _check_one(path):
    try:
        p = PakFile(path)
        notes = p.consistency()
        if p.to_bytes() != p.data:
            return path, p.header.version, len(p.maps), "réécriture différente", notes
        return path, p.header.version, len(p.maps), None, notes
    except (setfile.FormatError, mapfile.FormatError) as e:
        return path, None, 0, f"{type(e).__name__} : {e}", []


def cmd_check(paths):
    files, skipped = [], []
    for path in setfile.find(paths, ".pak"):
        with open(path, "rb") as fh:
            head = fh.read(4)
        # les morceaux de l'installeur du correctif 1.1 portent ce nom sans en avoir le format
        (files if head == setfile.MAGIC.to_bytes(4, "little") else skipped).append(path)
    for path in skipped:
        print(f"  ignoré (pas de signature 0x22A7) : {path}")
    start = time.time()
    with multiprocessing.Pool() as pool:
        results = pool.map(_check_one, files)
    failed = [r for r in results if r[3]]
    for path, _, _, problem, _ in failed:
        print(f"  ÉCHEC {path} : {problem}")
    for path, _, _, _, notes in results:
        for note in notes:
            print(f"  remarque {path} : {note}")
    versions = sorted({r[1] for r in results if r[1] is not None})
    print(f"{len(files)} aventures .pak (versions {', '.join(map(str, versions))}), "
          f"{sum(r[2] for r in results)} cartes : {len(files) - len(failed)} réécrites à l'octet "
          f"près, {len(failed)} en échec ({time.time() - start:.0f} s)")
    sys.exit(1 if failed else 0)


def main(argv):
    if len(argv) == 3 and argv[1] == "info":
        cmd_info(argv[2])
    elif len(argv) >= 3 and argv[1] == "check":
        cmd_check(argv[2:])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
