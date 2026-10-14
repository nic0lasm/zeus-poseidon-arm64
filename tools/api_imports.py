#!/usr/bin/env python3
"""Régénère analysis/notes/api-imports.md : fonctions Windows importées par l'exécutable du jeu
et état de leur réalisation dans src/win32/.

Usage : python3 tools/api_imports.py [EXE]   (défaut : original/gog/app/Zeus.exe)
"""
import glob
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SILENT = {"mss32.dll", "binkw32.dll"}  # fonctions non écrites : sans effet, renvoient 0
ROLES = {
    "kernel32.dll": ("fichiers, mémoire, temps, environnement", "`kernel32.cpp`, fichiers par `vfs.cpp`"),
    "mss32.dll": ("son : Miles Sound System", "`mss.cpp` sur le mixeur de `audio.cpp` (SDL2, AudioToolbox) ; un fournisseur 3D logiciel"),
    "user32.dll": ("fenêtre, messages, souris, clavier", "`user32.cpp` et `desktop.cpp` (SDL2)"),
    "binkw32.dll": ("vidéos Bink", "`bink.cpp`, décodage par ffmpeg"),
    "gdi32.dll": ("capacités d'écran, palette", "`user32.cpp`"),
    "ijl10.dll": ("décodage JPEG (par numéro : 2 `ijlInit`, 3 `ijlFree`, 4 `ijlRead`)", "`media.cpp`, par ImageIO de macOS"),
    "ole32.dll": ("initialisation COM", "sans effet"),
    "shell32.dll": ("ouverture d'un lien", "sans effet"),
    "ddraw.dll": ("affichage (surfaces DirectDraw, interfaces COM)", "`ddraw.cpp`, tampon 16 bits 5-5-5"),
    "winmm.dll": ("horloge", "`user32.cpp`"),
}


def main(argv):
    exe = argv[1] if len(argv) > 1 else "original/gog/app/Zeus.exe"
    out = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "pe_triage.py"), exe, "--imports"],
                         capture_output=True, text=True, check=True, cwd=ROOT).stdout
    libs = {}
    for line in out.splitlines():
        m = re.match(r"\[(\S+)\] (.*)", line)
        if m:
            libs[m.group(1).lower()] = sorted(m.group(2).split(), key=str.lower)
    defined = set()
    for path in glob.glob(os.path.join(ROOT, "src", "win32", "*.cpp")):
        with open(path, encoding="utf-8") as f:
            for m in re.finditer(r'\bdef(?:_in)?\(\s*((?:"[^"]*",\s*)+)\d', f.read()):
                defined.add(re.findall(r'"([^"]*)"', m.group(1))[-1])
    total = sum(len(v) for v in libs.values())
    written = sum(1 for v in libs.values() for f in v if f in defined)
    order = sorted(libs, key=lambda d: (-len(libs[d]), d))
    lines = [
        "# Fonctions Windows importées par `Zeus.exe` (édition GOG)", "",
        "Produit par `tools/api_imports.py` à partir de `tools/pe_triage.py --imports` et du code de",
        "`src/win32/`. C'est la liste des fonctions que la couche hôte fournit au code d'origine.", "",
        "Une fonction **non écrite** arrête le programme en la nommant le jour où le jeu l'appelle.",
        "Seules les fonctions de son et de vidéo non écrites sont sans effet et renvoient 0.", "",
        f"Total : {total} fonctions dans {len(libs)} bibliothèques, dont {written} écrites.", "",
        "| Bibliothèque | Nombre | Écrites | Rôle | Réalisation |", "|---|---|---|---|---|",
    ]
    for dll in order:
        role, how = ROLES.get(dll, ("", ""))
        done = sum(1 for f in libs[dll] if f in defined)
        lines.append(f"| `{dll}` | {len(libs[dll])} | {done} | {role} | {how} |")
    lines.append("")
    for dll in order:
        names = libs[dll]
        missing = [f for f in names if f not in defined]
        lines += [f"## `{dll}` ({len(names)})", "", ", ".join(f"`{f}`" for f in names), ""]
        if missing:
            label = "Sans effet pour l'instant (renvoient 0)" if dll in SILENT else "Non écrites"
            lines += [f"{label} : " + ", ".join(f"`{f}`" for f in missing) + ".", ""]
    with open(os.path.join(ROOT, "analysis", "notes", "api-imports.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"{written} fonctions écrites sur {total}")


if __name__ == "__main__":
    main(sys.argv)
