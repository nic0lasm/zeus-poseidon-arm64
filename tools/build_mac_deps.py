#!/usr/bin/env python3
"""Compile pour macOS 11 les bibliothèques que l'application embarque, dans build/deps/mac.

Celles de Homebrew sont compilées pour le système de la machine qui les a construites
(macOS 14 sur Apple Silicon, 15 sur Intel chez GitHub) : un paquet qui les embarque ne démarre
pas sur un Mac plus ancien. Ce script construit depuis leurs sources, empreintes vérifiées, avec
MACOSX_DEPLOYMENT_TARGET=11.0 : SDL 2, Unicorn 2.1.4 (x86 seulement) et innoextract, avec en
bibliothèques statiques liblzma et la partie de Boost dont il a besoin. Les bibliothèques déjà
construites sont sautées ; zlib, bzip2 et iconv sont ceux du système.

Usage : python3 tools/build_mac_deps.py
Puis  : MACOSX_DEPLOYMENT_TARGET=11.0 cmake -S . -B build/diffusion -G Ninja -DCMAKE_BUILD_TYPE=Release \\
          -DCMAKE_PREFIX_PATH=$PWD/build/deps/mac -DZEUS_FFMPEG=build/deps/ffmpeg
        (avec PKG_CONFIG_PATH=$PWD/build/deps/mac/lib/pkgconfig, pour Unicorn)
"""
import hashlib
import os
import subprocess
import sys
import tarfile
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEPS = os.path.join(ROOT, "build", "deps")
PREFIX = os.path.join(DEPS, "mac")
TARGET = "11.0"

# nom, version, adresse, SHA-256
SOURCES = {
    "sdl": ("2.32.10", "https://github.com/libsdl-org/SDL/releases/download/release-{v}/SDL2-{v}.tar.gz",
            "5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165"),
    "unicorn": ("2.1.4", "https://files.pythonhosted.org/packages/b2/1b/b4248aa8422e86de690cf8e85cf8feae4c33405a097d1ebe71570bdaa6f5/"
                "unicorn-{v}.tar.gz", "00567a70e323f749b419cd86bee4f9115beab7ebba32194581c090cbb7c59cff"),
    "xz": ("5.8.1", "https://github.com/tukaani-project/xz/releases/download/v{v}/xz-{v}.tar.gz",
           "507825b599356c10dca1cd720c9d0d0c9d5400b9de300af00e4d1ea150795543"),
    "boost": ("1.83.0", "https://github.com/boostorg/boost/releases/download/boost-{v}/boost-{v}.tar.xz",
              "c5a0688e1f0c05f354bbd0b32244d36085d9ffc9f932e8a18983a9908096f614"),
    "innoextract": ("1.9", "https://github.com/dscharrer/innoextract/releases/download/{v}/innoextract-{v}.tar.gz",
                    "6344a69fc1ed847d4ed3e272e0da5998948c6b828cb7af39c6321aba6cf88126"),
}


def run(*args, cwd=None):
    print("+", " ".join(args), flush=True)
    if subprocess.run(args, cwd=cwd).returncode != 0:
        sys.exit(f"échec : {' '.join(args)}")


def sources(name):
    """Dossier des sources de `name`, téléchargées et déballées au besoin."""
    version, url, sha256 = SOURCES[name]
    url = url.format(v=version)
    archive = os.path.join(DEPS, os.path.basename(url))
    if not os.path.exists(archive):
        print("téléchargement de", url, flush=True)
        urllib.request.urlretrieve(url, archive)
    with open(archive, "rb") as f:
        if hashlib.sha256(f.read()).hexdigest() != sha256:
            sys.exit(f"{archive} : empreinte inattendue, fichier à supprimer et retélécharger")
    with tarfile.open(archive) as tar:
        top = tar.getnames()[0].split("/")[0]
        folder = os.path.join(DEPS, top)
        if not os.path.isdir(folder):
            tar.extractall(DEPS, filter="data")
    return folder


def done(name):
    return os.path.exists(os.path.join(PREFIX, f".{name}-{SOURCES[name][0]}"))


def mark(name):
    open(os.path.join(PREFIX, f".{name}-{SOURCES[name][0]}"), "w").close()


def cmake(name, source, *options):
    build = os.path.join(DEPS, f"build-{name}")
    # CMAKE_POLICY_VERSION_MINIMUM : les CMakeLists d'innoextract demandent CMake 2.8, que CMake 4 refuse.
    run("cmake", "-S", source, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_INSTALL_PREFIX={PREFIX}", "-DCMAKE_INSTALL_LIBDIR=lib", f"-DCMAKE_PREFIX_PATH={PREFIX}",
        f"-DCMAKE_OSX_DEPLOYMENT_TARGET={TARGET}", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5", *options)
    run("cmake", "--build", build)
    run("cmake", "--install", build)


def main():
    os.makedirs(PREFIX, exist_ok=True)
    os.environ["MACOSX_DEPLOYMENT_TARGET"] = TARGET
    # Les chemins des sources (__FILE__ des assertions) deviennent relatifs : le paquet ne doit citer
    # aucun dossier de la machine de construction.
    flags = f"-O2 -ffile-prefix-map={DEPS}=deps"
    if sys.platform == "darwin":
        flags += f" -mmacosx-version-min={TARGET}"
    for variable in ("CFLAGS", "CXXFLAGS"):
        os.environ[variable] = f"{os.environ.get(variable, '')} {flags}".strip()

    if not done("sdl"):
        cmake("sdl", sources("sdl"), "-DSDL_SHARED=ON", "-DSDL_STATIC=OFF", "-DSDL_TEST=OFF")
        mark("sdl")
    if not done("unicorn"):
        cmake("unicorn", os.path.join(sources("unicorn"), "src"), "-DUNICORN_ARCH=x86", "-DUNICORN_BUILD_TESTS=OFF")
        mark("unicorn")
    if not done("xz"):
        run("./configure", f"--prefix={PREFIX}", "--disable-shared", "--enable-static", "--disable-xz",
            "--disable-xzdec", "--disable-lzmadec", "--disable-lzmainfo", "--disable-lzma-links",
            "--disable-scripts", "--disable-doc", "--disable-nls", cwd=sources("xz"))
        run("make", f"-j{os.cpu_count() or 4}", "install", cwd=sources("xz"))
        mark("xz")
    if not done("boost"):
        boost = sources("boost")
        run("./bootstrap.sh", "--with-libraries=iostreams,filesystem,date_time,system,program_options",
            f"--prefix={PREFIX}", cwd=boost)
        # Ni lzma ni zstd dans Boost.Iostreams : innoextract n'en a pas besoin par lui, et ceux
        # de Homebrew seraient pris.
        link = [f"linkflags=-mmacosx-version-min={TARGET}"] if sys.platform == "darwin" else []
        run("./b2", "install", f"-j{os.cpu_count() or 4}", "link=static", "variant=release", "threading=multi",
            "-sNO_LZMA=1", "-sNO_ZSTD=1", f"cflags={flags}", f"cxxflags={flags}", *link, cwd=boost)
        mark("boost")
    if not done("innoextract"):
        cmake("innoextract", sources("innoextract"), "-DBoost_USE_STATIC_LIBS=ON", "-DLZMA_USE_STATIC_LIBS=ON",
              f"-DBOOST_ROOT={PREFIX}", "-DBoost_NO_SYSTEM_PATHS=ON", "-DUSE_LTO=OFF")
        mark("innoextract")
    print("bibliothèques pour macOS", TARGET, "installées dans", PREFIX)


if __name__ == "__main__":
    sys.exit(main())
