#!/usr/bin/env python3
"""Compile un FFmpeg réduit aux décodeurs Bink, sous licence LGPL, dans build/deps/ffmpeg.

Le FFmpeg de Homebrew est compilé sous GPL version 3 : il convient pour compiler et jouer chez
soi, pas pour distribuer une application qui embarque aussi Unicorn (GPL-2.0). Cette version
réduite, en bibliothèques statiques, ne contient que ce que le jeu utilise : le démultiplexeur
Bink, ses décodeurs vidéo et audio, et les conversions d'image et de son.

Usage : python3 tools/build_ffmpeg.py
Puis  : cmake -S . -B build -G Ninja -DZEUS_FFMPEG=build/deps/ffmpeg && cmake --build build
"""
import hashlib
import os
import subprocess
import sys
import tarfile
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERSION = "9.0.2"
URL = f"https://ffmpeg.org/releases/ffmpeg-{VERSION}.tar.xz"
SHA256 = "8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e"

CONFIGURE = [
    "--disable-everything", "--disable-programs", "--disable-doc", "--disable-network",
    "--disable-autodetect", "--disable-avdevice", "--disable-avfilter",
    "--enable-static", "--disable-shared", "--enable-pic",
    "--enable-demuxer=bink", "--enable-decoder=bink,binkaudio_rdft,binkaudio_dct",
    "--enable-protocol=file", "--enable-swscale", "--enable-swresample",
]


def main():
    deps = os.path.join(ROOT, "build", "deps")
    os.makedirs(deps, exist_ok=True)
    archive = os.path.join(deps, f"ffmpeg-{VERSION}.tar.xz")
    if not os.path.exists(archive):
        print("téléchargement de", URL)
        urllib.request.urlretrieve(URL, archive)
    with open(archive, "rb") as f:
        if hashlib.sha256(f.read()).hexdigest() != SHA256:
            raise SystemExit(f"{archive} : empreinte inattendue, archive à supprimer et retélécharger")
    source = os.path.join(deps, f"ffmpeg-{VERSION}")
    if not os.path.isdir(source):
        with tarfile.open(archive) as tar:
            tar.extractall(deps)
    prefix = os.path.join(deps, "ffmpeg")
    subprocess.run(["./configure", f"--prefix={prefix}"] + CONFIGURE, cwd=source, check=True)
    subprocess.run(["make", f"-j{os.cpu_count() or 4}"], cwd=source, check=True)
    subprocess.run(["make", "install"], cwd=source, check=True)
    print("FFmpeg réduit installé dans", prefix)


if __name__ == "__main__":
    sys.exit(main())
