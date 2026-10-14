#!/usr/bin/env python3
"""Lecture des fichiers d'aventure (.pak et .set) du jeu.

Usage :
    adventures.py info  FICHIER             affiche les informations du fichier
    adventures.py check DOSSIER             vérifie tous les .pak/.set trouvés
"""
import os
import sys
import struct

def identify(path):
    with open(path, "rb") as fh:
        d = fh.read(64)
    if d.startswith(b"MAPS"):
        return "MAPS container (map/save)"

    # Try identifying by signature
    signature = d[:4]
    if signature == b"\x01\x49\x2a\x6d":
        return "Legacy Pak (01492a6d)"
    if signature == b"\xa7\x22\x00\x00":
        return "Standard Adventure (a7220000)"

    return f"unknown: {d.hex()[:16]}..."

def cmd_info(path):
    print(f"{os.path.basename(path)} : {identify(path)}")

def cmd_check(root):
    for folder, _, names in sorted(os.walk(root)):
        for name in sorted(names):
            ext = os.path.splitext(name)[1].lower()
            if ext not in (".pak", ".set"):
                continue
            path = os.path.join(folder, name)
            print(f"{path} : {identify(path)}")

def main(argv):
    if len(argv) == 3 and argv[1] == "info":
        cmd_info(argv[2])
    elif len(argv) == 3 and argv[1] == "check":
        cmd_check(argv[2])
    else:
        sys.exit(__doc__)

if __name__ == "__main__":
    main(sys.argv)
