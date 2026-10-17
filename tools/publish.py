#!/usr/bin/env python3
"""Fusion d'une PR et préparation d'une version publiée, en une commande chacune.

Usage :
  python3 tools/publish.py merge N      fusionne la PR N
  python3 tools/publish.py release X.Y  prépare l'archive de la version X.Y

merge : attend la fin de la CI de la PR N (gh), s'arrête si une vérification a échoué ; fusionne
la branche dans main en local, par avance rapide ; vérifie que tout l'historique n'a qu'une
identité (nic0lasm, adresse noreply) ; pousse main, supprime la branche distante et locale.
GitHub marque la PR fusionnée d'elle-même.

release : construit build/diffusion, assemble « Zeus Poseidon.app » (make_app.py), vérifie sa
signature et lance son --selftest, compresse l'archive avec ditto, cherche en octets dans chaque
fichier de l'archive les mots qui ne doivent jamais être publiés, et affiche le SHA-256. La
version GitHub (tag, notes, envoi de l'archive) reste créée à la main.

Chaque étape s'arrête à la première anomalie ; la sortie tient en quelques lignes.
"""
import hashlib
import os
import platform
import shutil
import subprocess
import sys
import time
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IDENTITY = "nic0lasm 106993577+nic0lasm@users.noreply.github.com"
FORBIDDEN = [b"/Users/nico", b"Claude", b"claude", b"maigre", b"nicolas", b"anthropic"]
DIFFUSION = os.path.join(ROOT, "build", "diffusion")
APP = os.path.join(DIFFUSION, "Zeus Poseidon.app")


def run(*args, cwd=ROOT):
    """Lance une commande (dans le dépôt par défaut) ; s'arrête en citant sa sortie si elle échoue."""
    result = subprocess.run(args, cwd=cwd, capture_output=True, text=True)
    if result.returncode != 0:
        tail = (result.stderr or result.stdout).strip().splitlines()[-5:]
        sys.exit(f"échec : {' '.join(args)}\n" + "\n".join(tail))
    return result.stdout.strip()


def merge(number):
    import json
    branch = run("gh", "pr", "view", number, "--json", "headRefName", "--jq", ".headRefName")
    if branch in ("", "main"):
        sys.exit(f"PR {number} : branche « {branch} » inattendue")
    # Attente de la CI du dernier commit de la branche : toutes les vérifications terminées (au
    # moins une). Une PR en conflit avec main n'a pas de CI : on s'arrête au lieu d'attendre.
    local = run("git", "rev-parse", f"refs/heads/{branch}")
    while True:
        pr = json.loads(run("gh", "pr", "view", number, "--json", "statusCheckRollup,mergeable,headRefOid"))
        if pr["mergeable"] == "CONFLICTING":
            sys.exit(f"PR {number} en conflit avec main : fusionner origin/main dans {branch}, pousser, relancer")
        if pr["headRefOid"] != local:
            sys.exit(f"PR {number} : la branche locale {branch} diffère de celle de GitHub (pousser d'abord)")
        checks = pr["statusCheckRollup"]
        if checks and all(c.get("status") == "COMPLETED" for c in checks):
            break
        time.sleep(15)
    failed = [c.get("name", "?") for c in checks if c.get("conclusion") not in ("SUCCESS", "SKIPPED", "NEUTRAL")]
    if failed:
        sys.exit(f"PR {number} : CI en échec ({', '.join(failed)}), rien n'est fusionné")
    print(f"PR {number} ({branch}) : CI réussie ({len(checks)} vérification(s))")
    # La fusion se fait dans le dossier où main est extraite (le dépôt principal, ou ce dossier).
    main_dir, current = None, None
    for line in run("git", "worktree", "list", "--porcelain").splitlines():
        if line.startswith("worktree "):
            current = line[len("worktree "):]
        elif line == "branch refs/heads/main":
            main_dir = current
    if main_dir is None:
        main_dir = ROOT
        run("git", "checkout", "main")
    run("git", "fetch", "origin", "main", cwd=main_dir)
    # Avance rapide de main jusqu'à origin/main puis jusqu'à la branche, sans rien réécrire.
    run("git", "merge", "--ff-only", "origin/main", cwd=main_dir)
    if subprocess.run(["git", "merge-base", "--is-ancestor", "main", branch], cwd=main_dir).returncode != 0:
        sys.exit(f"main a avancé depuis la branche : fusionner origin/main dans {branch}, pousser, relancer")
    run("git", "merge", "--ff-only", branch, cwd=main_dir)
    identities = sorted(set(run("git", "log", "--format=%an %ae %cn %ce", "main", cwd=main_dir).splitlines()))
    if identities != [f"{IDENTITY} {IDENTITY}"]:
        sys.exit("identités inattendues dans l'historique, rien n'est poussé :\n" + "\n".join(identities))
    head = run("git", "rev-parse", "--short", "main", cwd=main_dir)
    print(f"main avancée jusqu'à {head}, identité unique vérifiée")
    run("git", "push", "origin", "main", cwd=main_dir)
    run("git", "push", "origin", "--delete", branch, cwd=main_dir)
    # Une branche encore extraite dans un worktree se garde ; sinon on la supprime.
    if f"branch refs/heads/{branch}" in run("git", "worktree", "list", "--porcelain"):
        print(f"main poussée, branche distante {branch} supprimée (locale gardée : extraite dans un worktree)")
        return
    run("git", "branch", "-d", branch, cwd=main_dir)
    print(f"main poussée, branche {branch} supprimée")


def release(version):
    run("cmake", "--build", DIFFUSION, "--target", "zeus-run")
    run(sys.executable, os.path.join(ROOT, "tools", "make_app.py"), "--binary", os.path.join(DIFFUSION, "zeus-run"),
        "--version", version, APP)
    run("codesign", "--verify", "--deep", "--strict", APP)
    run(os.path.join(APP, "Contents", "MacOS", "zeus-poseidon"), "--selftest")
    # Sans innoextract embarqué, l'application ne sait pas ouvrir un installateur GOG (#134).
    helper = os.path.join(APP, "Contents", "Helpers", "innoextract")
    if not os.path.exists(helper):
        sys.exit("innoextract absent du paquet : brew install innoextract, puis recommencer")
    inno = run(helper, "--version").splitlines()[0]
    print(f"application {version} construite, signature vérifiée, --selftest réussi, {inno} embarqué")
    # Suffixe selon le processeur de la machine qui construit : arm64 (Apple Silicon) ou intel.
    suffix = "macos-arm64" if platform.machine() == "arm64" else "macos-intel"
    name = f"Zeus-Poseidon-{version}-{suffix}.zip"
    archive = os.path.join(DIFFUSION, name)
    if os.path.exists(archive):
        # Une archive déjà publiée ne se remplace pas : seule celle d'une version sans tag peut l'être.
        if run("git", "tag", "-l", f"v{version}"):
            sys.exit(f"{name} existe et la version v{version} est déjà publiée")
        os.remove(archive)
    run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", APP, archive)
    hits = []
    with zipfile.ZipFile(archive) as z:
        for info in z.infolist():
            data = z.read(info)
            hits += [f"{info.filename} : {w.decode()}" for w in FORBIDDEN if w in data]
        count = len(z.infolist())
    if hits:
        sys.exit(f"{name} à ne pas publier :\n" + "\n".join(hits[:10]))
    with open(archive, "rb") as f:
        digest = hashlib.sha256(f.read()).hexdigest()
    print(f"{name} : {os.path.getsize(archive) / 1e6:.1f} Mo, {count} fichiers, aucun mot interdit")
    # Même archive sous un nom sans version : le lien releases/latest/download/… ne change jamais.
    stable = os.path.join(DIFFUSION, f"Zeus-Poseidon-{suffix}.zip")
    shutil.copyfile(archive, stable)
    print(f"{os.path.basename(stable)} : mêmes octets, à joindre à la version")
    print(f"SHA-256 {digest}")


def main(argv):
    if len(argv) != 3 or argv[1] not in ("merge", "release"):
        sys.exit("usage : publish.py merge N | release X.Y")
    (merge if argv[1] == "merge" else release)(argv[2])


if __name__ == "__main__":
    main(sys.argv)
