#!/usr/bin/env python3
"""Rejoue les scénarios de tests/scenarios et compare leurs images à une référence.

Usage : python3 tools/check_scenarios.py [record] [--exe ZEUS.EXE] [--no-native]

  record   enregistre la référence dans build/scenarios/ au lieu de comparer
  --exe    exécutable du jeu (défaut : original/gog/app/Zeus.exe)
  --no-native  passe --no-native à zeus-run : tout le code du jeu est émulé

Chaque scénario est joué avec une horloge fictive (zeus-run --deterministic) : deux exécutions
donnent les mêmes images, dont l'empreinte est écrite dans un journal. Un journal différent de
la référence signale un changement de comportement du jeu.
"""
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read_scenario(path):
    script, seconds, options = [], 60, []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line.startswith("#"):
                if "durée" in line:
                    seconds = int(line.split(":")[1])
                elif "options" in line:
                    options = line.split(":", 1)[1].split()
            elif line:
                script.append(line)
    return "; ".join(script), seconds, options


def main(argv):
    record = "record" in argv
    exe = argv[argv.index("--exe") + 1] if "--exe" in argv else os.path.join(ROOT, "original", "gog", "app", "Zeus.exe")
    extra = ["--no-native"] if "--no-native" in argv else []
    out = os.path.join(ROOT, "build", "scenarios")
    os.makedirs(out, exist_ok=True)
    failures = 0
    for path in sorted(glob.glob(os.path.join(ROOT, "tests", "scenarios", "*.txt"))):
        name = os.path.splitext(os.path.basename(path))[0]
        script, seconds, options = read_scenario(path)
        reference = os.path.join(out, name + ".reference.txt")
        log = reference if record else os.path.join(out, name + ".txt")
        run = 0
        write_dir = os.path.join(out, f"{name}-run{run}")
        while os.path.exists(write_dir):  # dossier d'écriture neuf : le jeu doit partir de zéro
            run += 1
            write_dir = os.path.join(out, f"{name}-run{run}")
        result = subprocess.run(
            [os.path.join(ROOT, "build", "zeus-run"), "--run", exe, "--headless", "--deterministic",
             "--write-dir", write_dir, "--seconds", str(seconds), "--frame-log", log, "--script", script] + options + extra,
            capture_output=True, text=True)
        last = result.stderr.strip().splitlines()[-1] if result.stderr.strip() else ""
        with open(log) as f:
            frames = sum(1 for _ in f)
        if result.returncode != 0:
            print(f"ÉCHEC   {name} : {last}")
            failures += 1
        elif record:
            print(f"référence enregistrée : {name}, {frames} images")
        elif not os.path.exists(reference):
            print(f"ÉCHEC   {name} : pas de référence (lancer d'abord « record »)")
            failures += 1
        else:
            with open(reference) as a, open(log) as b:
                same = a.read() == b.read()
            print(f"{'ok     ' if same else 'ÉCHEC  '} {name} : {frames} images" + ("" if same else ", journal différent de la référence"))
            failures += 0 if same else 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
