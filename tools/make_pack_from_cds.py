#!/usr/bin/env python3
"""Fabrique un pack de langue directement à partir des CD d'origine du jeu.

Usage : python3 tools/make_pack_from_cds.py DOSSIER REFERENCE_GOG SORTIE [--work TRAVAIL]

  DOSSIER        contient les images des deux CD (Zeus et Poséidon) et, si possible, les mises
                 à jour 1.1 et 2.1 de la même langue ; les fichiers sont reconnus à leur contenu
  REFERENCE_GOG  dossier « app » de l'installateur GOG déballé
  SORTIE         pack à créer (ne doit pas exister)
  --work         dossier de travail (défaut : SORTIE + « .travail ») ; créé puis supprimé

Enchaîne, sans rien exécuter des CD, les outils du projet : lecture des images, déballage des
archives d'installation, application des mises à jour, puis tools/make_language_pack.py.
Il faut environ 3 Go libres pendant l'opération.
"""
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import build_install  # noqa: E402
import cdimage  # noqa: E402
import ishield  # noqa: E402
import make_language_pack  # noqa: E402
import wise  # noqa: E402

STEPS = 7


def step(number, text):
    print(f"ÉTAPE {number}/{STEPS} : {text}", flush=True)


def find_sources(folder):
    """Reconnaît les images de CD et les mises à jour d'un dossier."""
    found = {}
    candidates = []
    for root, _, names in os.walk(folder):
        if root[len(folder):].count(os.sep) > 1:
            continue
        candidates += [os.path.join(root, n) for n in names if not n.startswith(".")]
    for path in sorted(candidates):
        size = os.path.getsize(path)
        if size > 100 << 20:  # image de CD ?
            try:
                volume = cdimage.Iso9660(cdimage.CdImage(path)).volume_id.upper()
            except (ValueError, OSError):
                continue
            if "POSEIDON" in volume:
                found.setdefault("cd-poseidon", path)
            elif "ZEUS" in volume:
                found.setdefault("cd-zeus", path)
        elif 256 << 10 < size < 64 << 20 and path.lower().endswith(".exe"):  # mise à jour Wise ?
            try:
                with open(path, "rb") as f:
                    described = wise.describe(f.read())
            except Exception:  # pas un installateur Wise
                continue
            # La mise à jour 2.1 se reconnaît à son fichier « lisez-moi » (Lisezmoi2_1, Readme2_1…).
            poseidon = any("2_1" in target or "2.1" in target for item in described for target in item[4])
            found.setdefault("patch-poseidon-2.1" if poseidon else "patch-zeus-1.1", path)
    return found


def find_file(folder, name):
    for n in os.listdir(folder):
        if n.lower() == name.lower():
            return os.path.join(folder, n)
    return None


def main(argv):
    args = argv[1:]
    work = None
    if "--work" in args:
        i = args.index("--work")
        work = args[i + 1]
        del args[i:i + 2]
    if len(args) != 3:
        raise SystemExit(__doc__)
    folder, reference, out = (os.path.abspath(a) for a in args)
    work = os.path.abspath(work or out + ".travail")
    if os.path.exists(out) or os.path.exists(work):
        raise SystemExit(f"{out} ou {work} existe déjà")

    step(1, "recherche des CD et des mises à jour")
    sources = find_sources(folder)
    for needed, label in (("cd-zeus", "du CD de Zeus"), ("cd-poseidon", "du CD de Poséidon")):
        if needed not in sources:
            raise SystemExit(f"image {label} introuvable dans {folder}")
    for optional, label in (("patch-zeus-1.1", "1.1"), ("patch-poseidon-2.1", "2.1")):
        if optional not in sources:
            print(f"attention : mise à jour {label} absente, le pack reprendra les fichiers du CD", flush=True)

    # Les outils n'écrivent que sous leur racine : ici, le dossier de travail.
    os.makedirs(work)
    for module in (cdimage, ishield, wise, build_install):
        module.PROJECT_ROOT = work
    build_install.ORIGINAL = work
    path = lambda name: os.path.join(work, name)  # noqa: E731
    try:
        step(2, "lecture du CD de Zeus")
        cdimage.cmd_extract(sources["cd-zeus"], path("cd-zeus"))
        step(3, "lecture du CD de Poséidon")
        cdimage.cmd_extract(sources["cd-poseidon"], path("cd-poseidon"))
        step(4, "déballage des archives d'installation")
        ishield.cmd_extract(find_file(path("cd-zeus"), "data1.cab"), path("cab-zeus"))
        ishield.cmd_extract(find_file(path("cd-poseidon"), "data1.cab"), path("cab-poseidon"))
        step(5, "application des mises à jour")
        for name in ("patch-zeus-1.1", "patch-poseidon-2.1"):
            if name in sources:
                wise.cmd_extract(sources[name], path(name))
        inst = build_install.Install(path("install"))
        os.makedirs(path("install"))
        inst.add_tree("cab-zeus")
        if "patch-zeus-1.1" in sources:
            inst.add_wise("patch-zeus-1.1")
        inst.add_tree("cab-poseidon", skip=build_install.INSTALLER_ONLY)
        if "patch-poseidon-2.1" in sources:
            inst.add_wise("patch-poseidon-2.1")
        step(6, "assemblage du pack de langue")
        make_language_pack.main(["", path("install"), reference, out, "--cd", path("cd-zeus"), "--cd", path("cd-poseidon")])
    finally:
        step(7, "nettoyage")
        shutil.rmtree(work, ignore_errors=True)
    print("TERMINÉ", flush=True)


if __name__ == "__main__":
    main(sys.argv)
