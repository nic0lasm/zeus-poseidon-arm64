#!/usr/bin/env python3
"""Assemble « Zeus Poseidon.app » à partir de build/zeus-run.

Le paquet embarque les bibliothèques dont le programme dépend (Unicorn, SDL, ffmpeg…), copiées
depuis Homebrew et rendues relatives au paquet, puis signe le tout localement. Il ne contient
aucun fichier du jeu : le dossier du jeu est demandé au premier lancement.

Usage : python3 tools/make_app.py [--binary PROGRAMME] [--version X.Y] [SORTIE.app]
        (défauts : build/zeus-run et build/Zeus Poseidon.app)

Pour un paquet redistribuable, donner un programme compilé avec le FFmpeg réduit de
tools/build_ffmpeg.py (cmake -DZEUS_FFMPEG=build/deps/ffmpeg).
"""
import os
import plistlib
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SYSTEM = ("/usr/lib/", "/System/")


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def dependencies(binary):
    """Bibliothèques non système dont dépend un binaire Mach-O."""
    lines = run("otool", "-L", binary).splitlines()[1:]
    paths = [line.strip().split(" (")[0] for line in lines]
    return [p for p in paths if not p.startswith(SYSTEM)]


def resolve(reference, user):
    """Chemin réel d'une dépendance citée par `user`."""
    folder = os.path.dirname(os.path.realpath(user))
    for prefix in ("@loader_path/", "@rpath/"):
        if reference.startswith(prefix):
            name = reference[len(prefix):]
            for base in (folder, "/opt/homebrew/lib"):
                if os.path.exists(os.path.join(base, name)):
                    return os.path.realpath(os.path.join(base, name))
            raise SystemExit(f"dépendance introuvable : {reference} (citée par {user})")
    return os.path.realpath(reference)


def main(argv):
    args = argv[1:]
    binary = os.path.join(ROOT, "build", "zeus-run")
    version = "0.0"
    if args[:1] == ["--binary"]:
        binary, args = os.path.abspath(args[1]), args[2:]
    if args[:1] == ["--version"]:
        version, args = args[1], args[2:]
    app = args[0] if args else os.path.join(ROOT, "build", "Zeus Poseidon.app")
    if not os.path.exists(binary):
        raise SystemExit(f"{binary} est absent : compiler d'abord")
    if os.path.exists(app):
        if not app.endswith(".app") or not os.path.exists(os.path.join(app, "Contents", "Info.plist")):
            raise SystemExit(f"{app} existe et n'est pas un paquet produit par cet outil")
        shutil.rmtree(app)
    macos = os.path.join(app, "Contents", "MacOS")
    frameworks = os.path.join(app, "Contents", "Frameworks")
    os.makedirs(macos)
    os.makedirs(frameworks)
    program = os.path.join(macos, "zeus-poseidon")
    shutil.copy2(binary, program)

    # sdl2-compat charge SDL3 à l'exécution, sans le déclarer, sous le nom « libSDL3.dylib »
    # cherché d'abord à côté de lui : on l'ajoute à la main, sous ce nom.
    extra = [p for p in ("/opt/homebrew/lib/libSDL3.dylib",) if os.path.exists(p)]

    copied = {}  # chemin réel d'origine -> copie dans le paquet
    pending = [(program, binary)] + [(None, e) for e in extra]

    # Outil de déballage des installateurs GOG, embarqué comme programme auxiliaire.
    helper = None
    for candidate in ("/opt/homebrew/bin/innoextract", "/usr/local/bin/innoextract"):
        if os.path.exists(candidate):
            helpers = os.path.join(app, "Contents", "Helpers")
            os.makedirs(helpers)
            helper = os.path.join(helpers, "innoextract")
            shutil.copy2(os.path.realpath(candidate), helper)
            os.chmod(helper, 0o755)
            pending.append((helper, os.path.realpath(candidate)))
            break
    if helper is None:
        print("attention : innoextract absent, le paquet ne saura pas ouvrir un installateur GOG")
    while pending:
        target, origin = pending.pop()
        if target is None:
            real = os.path.realpath(origin)
            if real in copied:
                continue
            target = os.path.join(frameworks, os.path.basename(origin))
            shutil.copy2(real, target)
            os.chmod(target, 0o755)
            copied[real] = target
            run("install_name_tool", "-id", "@executable_path/../Frameworks/" + os.path.basename(target), target)
            origin = real
        for reference in dependencies(target):
            if reference.startswith("@executable_path/"):
                continue
            real = resolve(reference, origin)
            if real == os.path.realpath(origin):
                continue
            name = os.path.basename(reference)
            if real not in copied:
                copy = os.path.join(frameworks, name)
                shutil.copy2(real, copy)
                os.chmod(copy, 0o755)
                copied[real] = copy
                run("install_name_tool", "-id", "@executable_path/../Frameworks/" + name, copy)
                pending.append((copy, real))
            run("install_name_tool", "-change", reference,
                "@executable_path/../Frameworks/" + os.path.basename(copied[real]), target)

    with open(os.path.join(app, "Contents", "Info.plist"), "wb") as f:
        plistlib.dump({
            "CFBundleName": "Zeus Poseidon",
            "CFBundleDisplayName": "Zeus + Poseidon",
            "CFBundleIdentifier": "community.zeus-poseidon.mac",
            "CFBundleExecutable": "zeus-poseidon",
            "CFBundlePackageType": "APPL",
            "CFBundleShortVersionString": version,
            "CFBundleVersion": version,
            "LSMinimumSystemVersion": "12.0",
            "LSApplicationCategoryType": "public.app-category.simulation-games",
            "NSHighResolutionCapable": True,
        }, f)

    # Les chemins de recherche hérités de Homebrew sont remplacés par le dossier du paquet :
    # c'est par eux que sdl2-compat trouve SDL3.
    # Fabrication des packs de langue : les outils Python du projet et un interpréteur autonome
    # (tools/fetch_python.py), réduit à ce dont ces outils ont besoin.
    python_source = os.path.join(ROOT, "build", "deps", "python-standalone", "python")
    signed_extra = []
    if os.path.isdir(python_source):
        python_target = os.path.join(app, "Contents", "Resources", "python")
        os.makedirs(os.path.join(python_target, "bin"))
        interpreter = os.path.join(python_target, "bin", "python3")
        shutil.copy2(os.path.join(python_source, "bin", "python3.12"), interpreter)
        signed_extra.append(interpreter)
        unused = {"site-packages", "ensurepip", "idlelib", "tkinter", "lib2to3", "pydoc_data", "unittest", "test",
                  "turtledemo", "config-3.12-darwin", "__pycache__", "venv", "distutils", "asyncio", "email",
                  "multiprocessing", "xml", "xmlrpc", "http", "sqlite3", "curses", "dbm", "wsgiref", "html",
                  "concurrent", "ctypes", "logging", "json", "tomllib", "zipapp.py", "pydoc.py", "turtle.py",
                  "_pydecimal.py", "doctest.py", "pdb.py"}
        shutil.copytree(os.path.join(python_source, "lib", "python3.12"), os.path.join(python_target, "lib", "python3.12"),
                        ignore=lambda folder, names: [n for n in names if n in unused])
        for folder, _, names in os.walk(python_target):
            signed_extra += [os.path.join(folder, n) for n in names if n.endswith((".so", ".dylib"))]
        tools_target = os.path.join(app, "Contents", "Resources", "tools")
        os.makedirs(tools_target)
        for name in ("cdimage.py", "ishield.py", "wise.py", "build_install.py", "make_language_pack.py",
                     "make_pack_from_cds.py"):
            shutil.copy2(os.path.join(ROOT, "tools", name), os.path.join(tools_target, name))
    else:
        print("attention : Python autonome absent (tools/fetch_python.py), le paquet ne saura pas fabriquer de pack de langue")

    # Licences : celle du projet et la liste des bibliothèques embarquées.
    resources = os.path.join(app, "Contents", "Resources")
    os.makedirs(resources, exist_ok=True)
    shutil.copy2(os.path.join(ROOT, "LICENSE"), os.path.join(resources, "LICENSE.txt"))
    shutil.copy2(os.path.join(ROOT, "docs", "third-party.md"), os.path.join(resources, "THIRD-PARTY.md"))

    # Les informations de mise au point contiennent les chemins de compilation, donc le nom du
    # dossier personnel de celui qui a compilé : on les retire de tout ce qui a été copié.
    for binary_file in [program] + ([helper] if helper else []) + sorted(copied.values()):
        run("strip", "-S", "-x", binary_file)

    for library in sorted(copied.values()):
        listing = run("otool", "-l", library).splitlines()
        for i, line in enumerate(listing):
            if line.strip() == "cmd LC_RPATH":
                run("install_name_tool", "-delete_rpath", listing[i + 2].split()[1], library)
        run("install_name_tool", "-add_rpath", "@loader_path", library)
        run("codesign", "--force", "--sign", "-", library)
    if helper:
        run("codesign", "--force", "--sign", "-", helper)
    for binary_file in signed_extra:
        run("codesign", "--force", "--sign", "-", binary_file)
    run("codesign", "--force", "--sign", "-", app)

    # Dernier contrôle : aucun fichier du paquet ne doit citer le dossier personnel.
    home = os.path.expanduser("~").encode()
    leaks = []
    for folder, _, files in os.walk(app):
        for name in files:
            with open(os.path.join(folder, name), "rb") as f:
                if home in f.read():
                    leaks.append(os.path.relpath(os.path.join(folder, name), app))
    if leaks:
        raise SystemExit("paquet à ne pas diffuser, il cite le dossier personnel : " + ", ".join(leaks))

    size = sum(os.path.getsize(os.path.join(d, f)) for d, _, files in os.walk(app) for f in files)
    print(f"{app} : {len(copied)} bibliothèques embarquées, {size / 1e6:.0f} Mo")


if __name__ == "__main__":
    main(sys.argv)
