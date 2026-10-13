#!/usr/bin/env python3
"""Reconstitue l'installation finale du jeu (Zeus 1.0 -> 1.1 -> Poséidon 2.0 -> 2.1).

Reproduit, sans rien exécuter, ce que font les quatre installateurs dans l'ordre :
    original/cab-zeus            fichiers de Zeus 1.0
    original/patch-zeus-1.1      mise à jour 1.1 (fichiers complets et correctifs binaires)
    original/cab-poseidon        fichiers de Poséidon 2.0
    original/patch-poseidon-2.1  mise à jour 2.1

Usage : build_install.py [DEST]     (défaut : original/install ; doit être vide ou absent)

Les noms de fichiers sont comparés sans tenir compte de la casse, comme sous Windows.
Les fichiers lus directement sur les CD (musiques, voix, vidéos) ne sont pas copiés.
"""
import csv
import os
import sys

import wise

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ORIGINAL = os.path.join(PROJECT_ROOT, "original")

# Fichiers du moteur d'installation InstallShield présents dans l'archive de Poséidon.
INSTALLER_ONLY = {"_isres.dll", "corecomp.ini", "ctor.dll", "ikernel.exe", "iscript.dll",
                  "isrt.dll", "iuser.dll", "objectps.dll", "value.shl", "default.pal",
                  "vssver.scc", "implogo small", "poseidon adventure"}


class Install:
    def __init__(self, dest):
        self.dest = dest
        self.files = {}  # chemin relatif en minuscules -> chemin relatif réel
        self.log = []

    def _real(self, rel):
        """Chemin réel pour rel, en réutilisant la casse des dossiers et fichiers existants."""
        key = rel.lower()
        if key in self.files:
            return self.files[key]
        parts = rel.split("/")
        for depth in range(len(parts) - 1, 0, -1):
            prefix = "/".join(parts[:depth]).lower() + "/"
            for known_key, known in self.files.items():
                if known_key.startswith(prefix):
                    return "/".join(known.split("/")[:depth] + parts[depth:])
        return rel

    def read(self, rel):
        real = self.files.get(rel.lower())
        if real is None:
            return None
        with open(os.path.join(self.dest, real), "rb") as fh:
            return fh.read()

    def write(self, rel, data, step):
        parts = rel.split("/")
        if any(p in ("", ".", "..") or ":" in p for p in parts):
            raise ValueError(f"chemin dangereux : {rel!r}")
        real = self._real(rel)
        existed = rel.lower() in self.files
        path = os.path.join(self.dest, real)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as fh:
            fh.write(data)
        self.files[rel.lower()] = real
        self.log.append((step, "remplacé" if existed else "ajouté", real, len(data)))

    def add_tree(self, name, skip=()):
        root = os.path.join(ORIGINAL, name)
        count = 0
        for folder, _, names in sorted(os.walk(root)):
            for n in sorted(names):
                if n == ".DS_Store" or n.lower() in skip or n.lower().startswith("pos_ins_"):
                    continue
                full = os.path.join(folder, n)
                with open(full, "rb") as fh:
                    self.write(os.path.relpath(full, root).replace(os.sep, "/"), fh.read(), name)
                count += 1
        print(f"{name:22} {count} fichiers copiés")

    def add_wise(self, name):
        root = os.path.join(ORIGINAL, name)
        full_files = patched = 0
        with open(os.path.join(root, "index.tsv"), encoding="utf-8") as fh:
            rows = list(csv.DictReader(fh, delimiter="\t"))
        for row in rows:
            targets = [t for t in row["destination"].split(" | ") if t.startswith("%MAINDIR%\\")]
            if not targets:
                continue
            with open(os.path.join(root, row["fichier"]), "rb") as fh:
                data = fh.read()
            for target in targets:
                rel = target[len("%MAINDIR%\\"):].replace("\\", "/")
                old = self.read(rel)
                if old is not None and wise.is_patch_for(data, old):
                    self.write(rel, wise.apply_patch(data, old), name)
                    patched += 1
                elif data[:1] == b"\x01" and old is not None and len(data) < len(old) // 2 \
                        and wise.patch_info(data)[1]:
                    raise wise.PatchError(f"{rel} : correctif probable, mais version installée inconnue")
                else:
                    self.write(rel, data, name)
                    full_files += 1
        print(f"{name:22} {full_files} fichiers complets, {patched} correctifs binaires appliqués")


def main(argv):
    dest = os.path.abspath(argv[1] if len(argv) > 1 else os.path.join(ORIGINAL, "install"))
    if os.path.commonpath([dest, PROJECT_ROOT]) != PROJECT_ROOT or dest == PROJECT_ROOT:
        sys.exit(f"refus : {dest} n'est pas un sous-dossier du projet")
    if os.path.exists(dest) and os.listdir(dest):
        sys.exit(f"refus : {dest} existe et n'est pas vide")
    os.makedirs(dest, exist_ok=True)
    inst = Install(dest)
    inst.add_tree("cab-zeus")
    inst.add_wise("patch-zeus-1.1")
    inst.add_tree("cab-poseidon", skip=INSTALLER_ONLY)
    inst.add_wise("patch-poseidon-2.1")
    total = sum(os.path.getsize(os.path.join(dest, r)) for r in inst.files.values())
    print(f"installation : {len(inst.files)} fichiers, {total / 1e6:.1f} Mo, dans {dest}")
    with open(os.path.join(ORIGINAL, "install-journal.tsv"), "w", encoding="utf-8") as fh:
        fh.write("étape\taction\tfichier\ttaille\n")
        for row in inst.log:
            fh.write("\t".join(map(str, row)) + "\n")


if __name__ == "__main__":
    main(sys.argv)
