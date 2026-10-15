#!/usr/bin/env python3
"""Prépare le banc des scénarios sur une machine Linux neuve, puis le lance, en une commande.

Usage : python3 tools/bench_setup.py [--apt] [--game DOSSIER] [-- ARGUMENTS DE check_scenarios.py]

  --apt    installe d'abord les paquets de construction (apt-get, avec sudo si besoin)
  --game   dossier du jeu (celui de Zeus.exe) : original/gog/app devient un lien vers lui
  --       ce qui suit est passé à tools/check_scenarios.py, lancé une fois tout construit

Unicorn 2.0.1, celui d'Ubuntu 24.04, ne sait pas faire des pages non exécutables : chaque
écriture en mémoire prend alors le chemin lent de l'émulateur (Cpu::enable_paging,
src/win32/cpu.cpp) et un scénario dure environ quatre fois plus longtemps. Si pkg-config ne trouve
pas Unicorn 2.1 ou plus, ce script construit Unicorn 2.1.4 depuis ses sources (l'archive publiée
sur PyPI, empreinte vérifiée ; x86 seulement, une vingtaine de secondes) dans build/deps/unicorn,
puis configure et construit le projet avec. Les étapes déjà faites sont sautées.

Exemple, dans le cloud, références partagées entre machines :
  python3 tools/bench_setup.py --apt --game /mnt/project-files/game/app -- --references /mnt/project-files/banc/references
"""
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEPS = os.path.join(ROOT, "build", "deps")
UNICORN = "2.1.4"
UNICORN_URL = ("https://files.pythonhosted.org/packages/b2/1b/b4248aa8422e86de690cf8e85cf8feae4c33405a097d1ebe71570bdaa6f5/"
               f"unicorn-{UNICORN}.tar.gz")
UNICORN_SHA256 = "00567a70e323f749b419cd86bee4f9115beab7ebba32194581c090cbb7c59cff"
PACKAGES = ["cmake", "ninja-build", "pkg-config", "libsdl2-dev", "libavformat-dev", "libavcodec-dev",
            "libswscale-dev", "libswresample-dev", "libavutil-dev", "zlib1g-dev"]


def run(*args, env=None):
    print("+", " ".join(args), flush=True)
    if subprocess.run(args, env=env).returncode != 0:
        sys.exit(f"échec : {' '.join(args)}")


def install_packages():
    sudo = [] if os.geteuid() == 0 else ["sudo"]
    run(*sudo, "apt-get", "update", "-qq")
    run(*sudo, "env", "DEBIAN_FRONTEND=noninteractive", "apt-get", "install", "-y", "-qq", *PACKAGES)


def unicorn_pkgconfig():
    """Dossier pkg-config d'un Unicorn 2.1 ou plus, construit ici au besoin ; None pour celui du système."""
    if subprocess.run(["pkg-config", "--atleast-version=2.1", "unicorn"]).returncode == 0:
        print("Unicorn du système :", subprocess.run(["pkg-config", "--modversion", "unicorn"],
                                                     capture_output=True, text=True).stdout.strip())
        return None
    prefix = os.path.join(DEPS, "unicorn")
    pkgconfig = os.path.join(prefix, "lib", "pkgconfig")
    if os.path.exists(os.path.join(pkgconfig, "unicorn.pc")):
        return pkgconfig
    os.makedirs(DEPS, exist_ok=True)
    archive = os.path.join(DEPS, f"unicorn-{UNICORN}.tar.gz")
    if not os.path.exists(archive):
        print("téléchargement de", UNICORN_URL, flush=True)
        urllib.request.urlretrieve(UNICORN_URL, archive)
    with open(archive, "rb") as f:
        if hashlib.sha256(f.read()).hexdigest() != UNICORN_SHA256:
            sys.exit(f"{archive} : empreinte inattendue, archive à supprimer et retélécharger")
    sources = os.path.join(DEPS, f"unicorn-{UNICORN}")
    shutil.rmtree(sources, ignore_errors=True)
    with tarfile.open(archive) as tar:
        tar.extractall(DEPS, filter="data")
    build = os.path.join(sources, "build")
    run("cmake", "-S", os.path.join(sources, "src"), "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
        "-DUNICORN_ARCH=x86", "-DUNICORN_BUILD_TESTS=OFF", f"-DCMAKE_INSTALL_PREFIX={prefix}")
    run("cmake", "--build", build)
    run("cmake", "--install", build)
    return pkgconfig


def build(pkgconfig):
    env = dict(os.environ)
    if pkgconfig:
        env["PKG_CONFIG_PATH"] = pkgconfig + os.pathsep + env.get("PKG_CONFIG_PATH", "")
    configure = ["cmake", "-S", ROOT, "-B", os.path.join(ROOT, "build"), "-G", "Ninja"]
    cache = os.path.join(ROOT, "build", "CMakeCache.txt")
    if os.path.exists(cache):
        with open(cache, encoding="utf-8") as f:
            found = re.search(r"^UNICORN_VERSION:\w+=(\d+)\.(\d+)", f.read(), re.M)
        if found and tuple(map(int, found.groups())) < (2, 1):
            # pkg_check_modules garde son premier résultat : on l'oublie pour trouver le nouveau.
            configure += ["-U", "UNICORN*", "-U", "__pkg_config_*UNICORN*", "-U", "pkgcfg_lib_UNICORN*"]
    run(*configure, env=env)
    run("cmake", "--build", os.path.join(ROOT, "build"), env=env)
    run(os.path.join(ROOT, "build", "zeus-run"), "--selftest")


def link_game(folder):
    folder = os.path.abspath(folder)
    if not os.path.exists(os.path.join(folder, "Zeus.exe")):
        sys.exit(f"{folder} : pas de Zeus.exe")
    link = os.path.join(ROOT, "original", "gog", "app")
    if os.path.islink(link) and os.path.realpath(link) == os.path.realpath(folder):
        return
    if os.path.lexists(link):
        sys.exit(f"{link} existe déjà : le déplacer ou le supprimer d'abord")
    os.makedirs(os.path.dirname(link), exist_ok=True)
    os.symlink(folder, link)
    print(f"{link} → {folder}")


def main(argv):
    rest = argv[argv.index("--") + 1:] if "--" in argv else None
    own = argv[1:argv.index("--")] if "--" in argv else argv[1:]
    if "--apt" in own:
        install_packages()
    if "--game" in own:
        link_game(own[own.index("--game") + 1])
    build(unicorn_pkgconfig())
    if rest is not None:
        return subprocess.run([sys.executable, os.path.join(ROOT, "tools", "check_scenarios.py")] + rest).returncode
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
