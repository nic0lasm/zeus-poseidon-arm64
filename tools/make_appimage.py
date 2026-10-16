#!/usr/bin/env python3
"""Construit l'application Linux, une AppImage, en une commande.

L'AppImage est un seul fichier exécutable qui contient le programme et les bibliothèques qu'une
distribution n'a pas forcément : Unicorn 2.1, SDL 2 (compilé ici, il charge X11, Wayland,
PulseAudio, PipeWire ou ALSA à l'exécution, selon ce que le système a), FFmpeg réduit (statique),
innoextract et ses bibliothèques, les outils Python de fabrication des packs de langue (lancés
avec le python3 du système). Elle ne contient aucun fichier du jeu : le dossier du jeu est
demandé au premier lancement, comme sur Mac.

Étapes : construit au besoin Unicorn 2.1.4 (tools/bench_setup.py), FFmpeg réduit
(tools/build_ffmpeg.py) et SDL 2 dans build/deps ;
compile le programme dans build/linux (Release, libstdc++ liée statiquement) ; assemble
build/linux/AppDir ; lance son --selftest ; fabrique l'AppImage avec appimagetool (version
épinglée, empreinte vérifiée) ; relance --selftest depuis l'AppImage ; cherche les mots qui ne
doivent jamais être publiés et affiche le SHA-256 et la version de glibc demandée.

Usage : python3 tools/make_appimage.py X.Y [--apt] [--system-ffmpeg]

  --apt            installe d'abord les paquets de construction (PACKAGES, apt-get, avec sudo)
  --system-ffmpeg  accepte le FFmpeg du système à la place du FFmpeg réduit de
                   tools/build_ffmpeg.py : pour un essai chez soi, pas pour publier.

Résultat : build/diffusion/Zeus-Poseidon-X.Y-linux-ARCH.AppImage, et la même sous le nom stable
Zeus-Poseidon-linux-ARCH.AppImage (lien releases/latest/download/… constant).
Construite sur Ubuntu 22.04 (workflow release-linux), elle demande glibc 2.35 ou plus.
"""
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import urllib.request
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import bench_setup  # noqa: E402  (construction d'Unicorn 2.1)
import build_ffmpeg  # noqa: E402
from publish import FORBIDDEN  # noqa: E402

DEPS = os.path.join(ROOT, "build", "deps")
BUILD = os.path.join(ROOT, "build", "linux")
APPDIR = os.path.join(BUILD, "AppDir")
DIFFUSION = os.path.join(ROOT, "build", "diffusion")

SDL = "2.32.10"
SDL_URL = f"https://github.com/libsdl-org/SDL/releases/download/release-{SDL}/SDL2-{SDL}.tar.gz"
SDL_SHA256 = "5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165"
APPIMAGETOOL = "1.9.0"
APPIMAGETOOL_SHA256 = {
    "x86_64": "46fdd785094c7f6e545b61afcfb0f3d98d8eab243f644b4b17698c01d06083d1",
    "aarch64": "04f45ea45b5aa07bb2b071aed9dbf7a5185d3953b11b47358c1311f11ea94a96",
}

# Paquets Ubuntu de construction : en-têtes des systèmes que SDL charge à l'exécution.
PACKAGES = ["cmake", "ninja-build", "pkg-config", "patchelf", "innoextract", "desktop-file-utils", "zlib1g-dev",
            "nasm", "libx11-dev", "libxext-dev", "libxrandr-dev", "libxcursor-dev", "libxi-dev", "libxss-dev",
            "libxfixes-dev", "libxkbcommon-dev", "libwayland-dev", "wayland-protocols", "libdecor-0-dev",
            "libegl-dev", "libgl-dev", "libdrm-dev", "libgbm-dev", "libpulse-dev", "libasound2-dev",
            "libpipewire-0.3-dev", "libdbus-1-dev", "libudev-dev", "libibus-1.0-dev"]

# Bibliothèques que toute distribution de bureau fournit, et qu'il ne faut pas embarquer : glibc,
# le compilateur, les pilotes graphiques et le son, l'affichage. Les autres sont embarquées.
SYSTEM = re.compile(r"^(linux-vdso|linux-gate|ld-linux|libc|libm|libdl|libpthread|librt|libresolv|libutil|"
                    r"libgcc_s|libstdc\+\+|libz|libGL|libEGL|libGLX|libGLdispatch|libOpenGL|libdrm|libgbm|"
                    r"libX|libxcb|libwayland|libasound|libpulse|libdbus|libsystemd|libudev|libexpat|"
                    r"libffi)[.\-_0-9]")

# Outils Python de fabrication des packs de langue (les mêmes que dans le paquet Mac).
TOOLS = ("cdimage.py", "ishield.py", "wise.py", "build_install.py", "make_language_pack.py", "make_pack_from_cds.py")


def run(*args, env=None, cwd=None):
    print("+", " ".join(args), flush=True)
    if subprocess.run(args, env=env, cwd=cwd).returncode != 0:
        sys.exit(f"échec : {' '.join(args)}")


def output(*args, env=None):
    return subprocess.run(args, check=True, capture_output=True, text=True, env=env).stdout


def download(url, path, sha256):
    if not os.path.exists(path):
        print("téléchargement de", url, flush=True)
        urllib.request.urlretrieve(url, path)
    with open(path, "rb") as f:
        if hashlib.sha256(f.read()).hexdigest() != sha256:
            sys.exit(f"{path} : empreinte inattendue, fichier à supprimer et retélécharger")


def build_sdl():
    """SDL 2 compilé ici, partagé, avec chargement à l'exécution des systèmes d'affichage et de son."""
    prefix = os.path.join(DEPS, "sdl2")
    if os.path.exists(os.path.join(prefix, "lib", "cmake", "SDL2", "SDL2Config.cmake")):
        return prefix
    os.makedirs(DEPS, exist_ok=True)
    archive = os.path.join(DEPS, f"SDL2-{SDL}.tar.gz")
    download(SDL_URL, archive, SDL_SHA256)
    sources = os.path.join(DEPS, f"SDL2-{SDL}")
    shutil.rmtree(sources, ignore_errors=True)
    # Les projets Android de l'archive contiennent des liens qui sortent du dossier : écartés.
    with tarfile.open(archive) as tar:
        tar.extractall(DEPS, filter=lambda member, path: None if "/android-project" in member.name
                       else tarfile.data_filter(member, path))
    build = os.path.join(sources, "build")
    run("cmake", "-S", sources, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DSDL_SHARED=ON",
        "-DSDL_STATIC=OFF", "-DSDL_TEST=OFF", f"-DCMAKE_INSTALL_PREFIX={prefix}", "-DCMAKE_INSTALL_LIBDIR=lib")
    with open(os.path.join(build, "CMakeCache.txt"), encoding="utf-8") as f:
        cache = f.read()
    for option in ("SDL_X11", "SDL_WAYLAND", "SDL_PULSEAUDIO", "SDL_ALSA"):
        if not re.search(rf"^{option}:BOOL=ON", cache, re.M):
            sys.exit(f"SDL sans {option} : installer les en-têtes (voir PACKAGES)")
    run("cmake", "--build", build)
    run("cmake", "--install", build)
    return prefix


def build_program(sdl, system_ffmpeg):
    ffmpeg = os.path.join(DEPS, "ffmpeg")
    if not os.path.exists(os.path.join(ffmpeg, "lib", "libavcodec.a")) and not system_ffmpeg:
        build_ffmpeg.main()
    env = dict(os.environ)
    unicorn = bench_setup.unicorn_pkgconfig()
    if unicorn:
        env["PKG_CONFIG_PATH"] = unicorn + os.pathsep + env.get("PKG_CONFIG_PATH", "")
    configure = ["cmake", "-S", ROOT, "-B", BUILD, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                 f"-DCMAKE_PREFIX_PATH={sdl}", "-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc"]
    if not system_ffmpeg:
        configure.append(f"-DZEUS_FFMPEG={ffmpeg}")
    run(*configure, env=env)
    run("cmake", "--build", BUILD, "--target", "zeus-run", env=env)
    return os.path.join(BUILD, "zeus-run")


def libraries(binary):
    """Bibliothèques à embarquer pour un exécutable : {nom cité : chemin réel}."""
    found = {}
    for line in output("ldd", binary).splitlines():
        line = line.strip()
        if "=>" not in line:
            continue
        name, target = (part.strip() for part in line.split("=>", 1))
        if target.startswith("not found"):
            sys.exit(f"{binary} : {name} introuvable")
        if not SYSTEM.match(name):
            found[name] = os.path.realpath(target.split(" (")[0])
    return found


def icon(path, size=256):
    """Icône dessinée ici (aucune image du jeu) : un éclair doré sur un carré bleu égéen arrondi."""
    bolt = [(0.58, 0.08), (0.28, 0.54), (0.47, 0.54), (0.38, 0.92), (0.74, 0.40), (0.54, 0.40), (0.66, 0.08)]

    def inside(x, y):
        hit = False
        for (x1, y1), (x2, y2) in zip(bolt, bolt[1:] + bolt[:1]):
            if (y1 > y) != (y2 > y) and x < x1 + (y - y1) * (x2 - x1) / (y2 - y1):
                hit = not hit
        return hit

    rows = []
    radius = 0.18
    for j in range(size):
        row = bytearray(b"\0")
        y = (j + 0.5) / size
        for i in range(size):
            x = (i + 0.5) / size
            dx = max(radius - x, 0, x - (1 - radius))
            dy = max(radius - y, 0, y - (1 - radius))
            if dx * dx + dy * dy > radius * radius:
                row += b"\0\0\0\0"
            elif inside(x, y):
                row += bytes((240, 190, 60, 255))
            else:
                shade = int(40 * y)
                row += bytes((20 + shade, 70 + shade, 140 + shade, 255))
        rows.append(bytes(row))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))


def assemble(binary):
    shutil.rmtree(APPDIR, ignore_errors=True)
    bindir = os.path.join(APPDIR, "usr", "bin")
    libdir = os.path.join(APPDIR, "usr", "lib")
    helpers = os.path.join(APPDIR, "usr", "Helpers")  # là où le programme cherche innoextract
    for folder in (bindir, libdir, helpers):
        os.makedirs(folder)
    program = os.path.join(bindir, "zeus-poseidon")
    shutil.copy2(binary, program)
    innoextract = shutil.which("innoextract")
    if not innoextract:
        sys.exit("innoextract absent : l'application ne saurait pas ouvrir un installateur GOG")
    helper = os.path.join(helpers, "innoextract")
    shutil.copy2(os.path.realpath(innoextract), helper)
    copied = []
    for executable in (program, helper):
        for name, real in libraries(executable).items():
            target = os.path.join(libdir, name)
            if not os.path.exists(target):
                shutil.copy2(real, target)
                os.chmod(target, 0o755)
                copied.append(target)
    for executable in (program, helper):
        run("patchelf", "--set-rpath", "$ORIGIN/../lib", executable)
    for library in copied:
        run("patchelf", "--set-rpath", "$ORIGIN", library)
    # Les informations de mise au point citent les chemins de compilation : retirées partout.
    for elf in [program, helper] + copied:
        run("strip", "--strip-debug", elf)

    # Outils des packs de langue : le programme les cherche dans usr/tools, avec /usr/bin/python3.
    tools = os.path.join(APPDIR, "usr", "tools")
    os.makedirs(tools)
    for name in TOOLS:
        shutil.copy2(os.path.join(ROOT, "tools", name), os.path.join(tools, name))
    docs = os.path.join(APPDIR, "usr", "share", "doc", "zeus-poseidon")
    os.makedirs(docs)
    shutil.copy2(os.path.join(ROOT, "LICENSE"), os.path.join(docs, "LICENSE.txt"))
    shutil.copy2(os.path.join(ROOT, "docs", "third-party.md"), os.path.join(docs, "THIRD-PARTY.md"))

    with open(os.path.join(APPDIR, "zeus-poseidon.desktop"), "w", encoding="utf-8") as f:
        f.write("[Desktop Entry]\nType=Application\nName=Zeus + Poseidon\n"
                "Comment=Zeus: Master of Olympus and Poseidon, from your own copy of the game\n"
                "Comment[fr]=Le Maître de l'Olympe : Zeus et Poséidon, depuis votre exemplaire du jeu\n"
                "Exec=zeus-poseidon\nIcon=zeus-poseidon\nCategories=Game;StrategyGame;\nTerminal=false\n")
    icon(os.path.join(APPDIR, "zeus-poseidon.png"))
    # AppRun lien vers le programme : /proc/self/exe désigne alors usr/bin, d'où le programme
    # trouve usr/Helpers et usr/tools.
    os.symlink("usr/bin/zeus-poseidon", os.path.join(APPDIR, "AppRun"))
    return [program, helper] + copied


def glibc_needed(elves):
    versions = set()
    for elf in elves:
        versions.update(re.findall(r"GLIBC_(\d+\.\d+(?:\.\d+)?)", output("objdump", "-T", elf)))
    return max(versions, key=lambda v: tuple(map(int, v.split("."))))


def check_words(folder):
    home = os.path.expanduser("~").encode()
    hits = []
    for base, _, names in os.walk(folder):
        for name in names:
            path = os.path.join(base, name)
            if os.path.islink(path):
                continue
            with open(path, "rb") as f:
                data = f.read()
            hits += [f"{os.path.relpath(path, folder)} : {w.decode()}" for w in FORBIDDEN + [home] if w in data]
    if hits:
        sys.exit("AppImage à ne pas publier :\n" + "\n".join(hits[:10]))


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    if len(args) != 1:
        sys.exit("usage : make_appimage.py X.Y [--apt] [--system-ffmpeg]")
    version = args[0]
    arch = os.uname().machine
    if arch not in APPIMAGETOOL_SHA256:
        sys.exit(f"architecture {arch} non prise en charge")
    # Les chemins des sources (__FILE__ des assertions de SDL et d'Unicorn) deviennent relatifs :
    # aucun chemin de la machine de construction dans l'AppImage.
    for flags in ("CFLAGS", "CXXFLAGS"):
        os.environ[flags] = f"{os.environ.get(flags, '')} -ffile-prefix-map={ROOT}=.".strip()
    if "--apt" in argv:
        sudo = [] if os.geteuid() == 0 else ["sudo"]
        run(*sudo, "apt-get", "update", "-qq")
        run(*sudo, "env", "DEBIAN_FRONTEND=noninteractive", "apt-get", "install", "-y", "-qq",
            *PACKAGES)
    binary = build_program(build_sdl(), "--system-ffmpeg" in argv)
    elves = assemble(binary)
    check_words(APPDIR)
    run(os.path.join(APPDIR, "AppRun"), "--selftest")

    tool = os.path.join(DEPS, f"appimagetool-{APPIMAGETOOL}-{arch}.AppImage")
    download(f"https://github.com/AppImage/appimagetool/releases/download/{APPIMAGETOOL}/appimagetool-{arch}.AppImage",
             tool, APPIMAGETOOL_SHA256[arch])
    os.chmod(tool, 0o755)
    os.makedirs(DIFFUSION, exist_ok=True)
    name = f"Zeus-Poseidon-{version}-linux-{arch}.AppImage"
    image = os.path.join(DIFFUSION, name)
    if os.path.exists(image):
        os.remove(image)
    # Sans FUSE (machines de CI, conteneurs) : l'outil se déballe lui-même avant de s'exécuter.
    env = dict(os.environ, APPIMAGE_EXTRACT_AND_RUN="1", ARCH=arch, VERSION=version)
    run(tool, "--no-appstream", APPDIR, image, env=env)
    run(image, "--selftest", env=env)
    with open(image, "rb") as f:
        data = f.read()
    stable = os.path.join(DIFFUSION, f"Zeus-Poseidon-linux-{arch}.AppImage")
    shutil.copyfile(image, stable)
    os.chmod(stable, 0o755)
    print(f"{name} : {len(data) / 1e6:.1f} Mo, glibc {glibc_needed(elves)} minimum, aucun mot interdit")
    print(f"{os.path.basename(stable)} : mêmes octets, à joindre à la version")
    print(f"SHA-256 {hashlib.sha256(data).hexdigest()}")


if __name__ == "__main__":
    main(sys.argv)
