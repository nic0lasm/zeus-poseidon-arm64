#!/usr/bin/env python3
"""Fabrique un pack de langue à partir d'une autre édition du jeu.

Le programme fait tourner l'exécutable de l'édition GOG (anglaise). Un pack de langue est un
dossier dont les fichiers remplacent ceux de l'installation GOG : textes, polices, voix,
aventures. Cet outil le construit en comparant une installation dans la langue voulue (par
exemple celle que reconstitue tools/build_install.py depuis les CD) à l'installation GOG, et
en y ajoutant les voix et vidéos restées sur les CD.

Usage : python3 tools/make_language_pack.py INSTALLATION REFERENCE_GOG SORTIE [--cd DOSSIER]... [--link]

  INSTALLATION   jeu installé dans la langue voulue, version 2.1
  REFERENCE_GOG  dossier « app » de l'installateur GOG déballé
  SORTIE         pack à créer (ne doit pas exister)
  --cd DOSSIER   contenu d'un CD : ses dossiers Audio et Binks complètent l'installation
  --link         liens symboliques au lieu de copies (essais)

Le pack ne contient que des fichiers du jeu : il est fait pour rester sur la machine du joueur.
"""
import filecmp
import os
import shutil
import sys

# Jamais dans un pack : programmes, documents, fichiers propres à un installateur.
SKIPPED_EXTENSIONS = {".exe", ".dll", ".ico", ".pdf", ".txt", ".bmp", ".inf", ".ini", ".asi", ".m3d",
                      ".hashdb", ".info", ".zip", ".isu", ".dat", ".url"}
KEPT_TEXT_FOLDERS = {"model", "adventures"}  # leurs .txt sont des données du jeu
# Dossiers repris en entier : leurs fichiers portent des noms traduits.
REPLACED = ["Adventures"]


def walk(root):
    """Fichiers d'un dossier : chemin relatif en minuscules -> chemin réel."""
    found = {}
    for folder, _, files in os.walk(root):
        for name in files:
            path = os.path.join(folder, name)
            found[os.path.relpath(path, root).lower()] = path
    return found


def wanted(relative):
    top = relative.split(os.sep)[0]
    extension = os.path.splitext(relative)[1]
    if extension == ".txt" and top in KEPT_TEXT_FOLDERS:
        return True
    return extension not in SKIPPED_EXTENSIONS


def main(argv):
    args = [a for a in argv[1:] if a != "--link"]
    link = "--link" in argv
    cds = []
    while "--cd" in args:
        i = args.index("--cd")
        cds.append(args[i + 1])
        del args[i:i + 2]
    if len(args) != 3:
        raise SystemExit(__doc__)
    install, reference, out = args
    if os.path.exists(out):
        raise SystemExit(f"{out} existe déjà")

    sources = {}
    for cd in cds:  # les CD d'abord : l'installation, mise à jour, l'emporte
        for top in os.listdir(cd):
            if top.lower() in ("audio", "binks"):
                for relative, path in walk(os.path.join(cd, top)).items():
                    sources[os.path.join(top.lower(), relative)] = path
    sources.update(walk(install))
    reference_files = walk(reference)
    replaced = {name.lower() for name in REPLACED}

    count = size = 0
    for relative, path in sorted(sources.items()):
        if not wanted(relative):
            continue
        top = relative.split(os.sep)[0]
        same = relative in reference_files and filecmp.cmp(path, reference_files[relative], shallow=False)
        if same and top not in replaced:
            continue
        # Le pack reprend la casse des dossiers de l'installation de référence.
        folder = os.path.dirname(relative)
        real_folder = folder
        for known in reference_files.values():
            known_folder = os.path.dirname(os.path.relpath(known, reference))
            if known_folder.lower() == folder:
                real_folder = known_folder
                break
        target = os.path.join(out, real_folder, os.path.basename(path))
        os.makedirs(os.path.dirname(target), exist_ok=True)
        if link:
            os.symlink(os.path.abspath(path), target)
        else:
            shutil.copyfile(path, target)
        count += 1
        size += os.path.getsize(path)
    # Table caractère -> glyphe de l'exécutable de cette langue (caractères 0x20 à 0xFF) :
    # on la reconnaît à ses lettres, « a » à « z » valant 1 à 26 et « A » à « Z » 27 à 52.
    executable = sources.get("zeus.exe")
    table = None
    if executable:
        with open(executable, "rb") as f:
            data = f.read()
        letters = bytes(range(27, 53))
        at = data.find(letters)
        while at >= 0:
            start = at - 0x41
            if data[start + 0x61:start + 0x7B] == bytes(range(1, 27)):
                table = data[start + 0x20:start + 0x100]
                break
            at = data.find(letters, at + 1)
    if table:
        with open(os.path.join(out, "table-caracteres.bin"), "wb") as f:
            f.write(table)
    else:
        print("attention : table de caractères introuvable, les accents seront mal affichés")
    with open(os.path.join(out, "remplace.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(REPLACED) + "\n")
    print(f"{out} : {count} fichiers, {size / 1e6:.0f} Mo ; dossiers remplacés : {', '.join(REPLACED)}")


if __name__ == "__main__":
    main(sys.argv)
