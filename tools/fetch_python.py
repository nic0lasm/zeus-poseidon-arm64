#!/usr/bin/env python3
"""Télécharge un interpréteur Python autonome pour l'embarquer dans l'application.

L'application fabrique les packs de langue avec les outils Python du projet. macOS ne fournit
pas Python sans les outils de développement : le paquet embarque donc le sien, une version
« python-build-standalone » de CPython (licence PSF), placée dans build/deps/python-standalone.

Usage : python3 tools/fetch_python.py
"""
import hashlib
import os
import shutil
import sys
import tarfile
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RELEASE = "20261009"
NAME = f"cpython-3.12.15+{RELEASE}-aarch64-apple-darwin-install_only_stripped.tar.gz"
URL = f"https://github.com/astral-sh/python-build-standalone/releases/download/{RELEASE}/{NAME.replace('+', '%2B')}"
SHA256 = "233f8e15255b3e1d1fd47ea5dd5230dfed803f371138e896e54d98c60ae19f6f"


def main():
    deps = os.path.join(ROOT, "build", "deps")
    os.makedirs(deps, exist_ok=True)
    archive = os.path.join(deps, "python-standalone.tar.gz")
    if not os.path.exists(archive):
        print("téléchargement de", URL)
        urllib.request.urlretrieve(URL, archive)
    with open(archive, "rb") as f:
        if hashlib.sha256(f.read()).hexdigest() != SHA256:
            raise SystemExit(f"{archive} : empreinte inattendue, archive à supprimer et retélécharger")
    target = os.path.join(deps, "python-standalone")
    if os.path.isdir(target):
        shutil.rmtree(target)
    with tarfile.open(archive) as tar:
        tar.extractall(target)
    print("Python autonome installé dans", target)


if __name__ == "__main__":
    sys.exit(main())
