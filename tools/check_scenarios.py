#!/usr/bin/env python3
"""Rejoue les scénarios de tests/scenarios et compare leurs images et l'état de la partie à une référence.

Usage : python3 tools/check_scenarios.py [record] [--exe ZEUS.EXE] [--no-native] [--jobs N]
                                         [--only NOM,…] [--verify CIBLE [--every N]]
                                         [--references DOSSIER]

  record   enregistre les références dans build/scenarios/ au lieu de comparer, avec le code
           d'origine seul (zeus-run --no-native) : c'est l'original émulé qui sert de juge
  --exe    exécutable du jeu (défaut : original/gog/app/Zeus.exe)
  --no-native  passe --no-native à zeus-run : tout le code du jeu est émulé
  --jobs N scénarios joués en même temps (défaut : nombre de cœurs moins un)
  --only   seulement ces scénarios (noms de fichiers sans .txt, séparés par des virgules)
  --verify vérifie aussi, pendant chaque scénario, les appels d'une fonction du jeu (zeus-run
           --verify ADRESSE ou DÉBUT:FIN) ; --every N n'en vérifie qu'un sur N
  --references  garde les références dans DOSSIER, partagé entre machines, au lieu de
           build/scenarios ; celles qui manquent sont d'abord enregistrées (voir plus bas)

Chaque scénario est joué avec une horloge fictive (zeus-run --deterministic) : deux exécutions
donnent les mêmes images, dont l'empreinte est écrite dans un journal, et passent par les mêmes
états de la partie, dont l'empreinte est écrite à chaque tour de simulation (zeus-run --state-log,
voir docs/equivalence.md). Un journal différent de la référence signale un changement de
comportement du jeu ; pour l'état, le premier tour différent et ses zones sont indiqués. Les
scénarios joués en parallèle ne partagent rien (chacun a son dossier d'écriture neuf) ; les
résultats sont affichés dans l'ordre alphabétique.

Références partagées (--references) : une référence ne dépend que de ce que l'original émulé
exécute, pas des fonctions réécrites. Elle est rangée dans DOSSIER/<clé>/, où la clé résume
l'exécutable du jeu, le code de l'hôte hors fonctions natives (src/win32/native*, et
src/win32/game_gog.h, qui n'en contient que les adresses), les versions d'Unicorn et de FFmpeg
et le système ; le nom de chaque fichier porte l'empreinte du script du scénario. Une branche qui
ne touche qu'aux fonctions natives retrouve donc les références de main, et une machine neuve
celles d'une autre machine identique. Celles qui manquent sont enregistrées avant la comparaison
(--no-native, puis renommées d'un coup : deux machines peuvent le faire en même temps).
« record » les réenregistre. DOSSIER/<clé>/cle.txt détaille la clé. Une référence périmée par un
changement de l'hôte que la clé ignore ne peut que signaler une différence, jamais en cacher une.
"""
import concurrent.futures
import glob
import hashlib
import os
import platform
import re
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


def read_lines(path):
    with open(path, encoding="utf-8") as f:
        return f.read().splitlines()


def file_hash(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def host_key(exe):
    """Clé des références partagées et son détail : ce dont dépend une exécution --no-native."""
    lines = [f"jeu {file_hash(exe)}", f"système {platform.system()} {platform.machine()}"]
    with open(os.path.join(ROOT, "build", "CMakeCache.txt"), encoding="utf-8") as f:
        lines += sorted(" ".join(m.groups()) for m in
                        re.finditer(r"^((?:UNICORN|FFMPEG_lib\w+)_VERSION|ZEUS_FFMPEG):\w+=(.*)$", f.read(), re.M))
    sources = [os.path.join(ROOT, "CMakeLists.txt")]
    for folder in ("formats", "game", "platform", "tools/zeus_run", "win32"):
        sources += glob.glob(os.path.join(ROOT, "src", folder, "*.*"))
    host = hashlib.sha256()
    for path in sorted(sources):
        name = os.path.relpath(path, ROOT).replace(os.sep, "/")
        if name.startswith("src/win32/native") or name == "src/win32/game_gog.h":
            continue
        host.update(name.encode() + b"\0" + bytes.fromhex(file_hash(path)))
    lines.append(f"hôte {host.hexdigest()}")
    return hashlib.sha256("\n".join(lines).encode()).hexdigest()[:16], lines


def state_difference(reference, log):
    """Premier tour où l'état diffère et noms des zones en cause, ou None si identiques."""
    expected, actual = read_lines(reference), read_lines(log)
    if expected == actual:
        return None
    zones = [line.split(" ", 5)[5] for line in expected if line.startswith("# zone ")]
    expected = [line.split() for line in expected if not line.startswith("#")]
    actual = [line.split() for line in actual if not line.startswith("#")]
    for a, b in zip(expected, actual):
        if a != b:
            names = [zones[i] for i in range(min(len(a), len(b)) - 1) if a[i + 1] != b[i + 1]]
            return f"état différent au tour {a[0]} : " + ", ".join(names[:6]) + ("…" if len(names) > 6 else "")
    return f"état : {len(actual)} tours au lieu de {len(expected)}"


def reference_paths(path, out, shared):
    """Journaux de référence (images, état) d'un scénario."""
    name = os.path.splitext(os.path.basename(path))[0]
    if shared:
        stem = os.path.join(shared, f"{name}-{file_hash(path)[:12]}")
        return stem + ".images.txt", stem + ".etat.txt"
    return os.path.join(out, name + ".reference.txt"), os.path.join(out, name + ".state.reference.txt")


def record_shared(path, out, exe, shared, again):
    """Enregistre la référence partagée d'un scénario si elle manque (toujours si `again`) ;
    renvoie (ligne, échec), ou None si elle existait."""
    frames_reference, state_reference = reference_paths(path, out, shared)
    if not again and os.path.exists(frames_reference) and os.path.exists(state_reference):
        return None
    suffix = f".{platform.node()}-{os.getpid()}.tmp"
    temporary = (frames_reference + suffix, state_reference + suffix)
    line, failed = play(path, out, exe, ["--no-native"], True, temporary)
    if failed:
        for name in temporary:
            if os.path.exists(name):
                os.remove(name)
        return line, True
    os.replace(temporary[0], frames_reference)
    os.replace(temporary[1], state_reference)
    return line, False


def play(path, out, exe, extra, record, references):
    """Joue un scénario ; renvoie (ligne de résultat, échec)."""
    name = os.path.splitext(os.path.basename(path))[0]
    script, seconds, options = read_scenario(path)
    frames_reference, state_reference = references
    frames_log = frames_reference if record else os.path.join(out, name + ".txt")
    state_log = state_reference if record else os.path.join(out, name + ".state.txt")
    run = 0
    write_dir = os.path.join(out, f"{name}-run{run}")
    while os.path.exists(write_dir):  # dossier d'écriture neuf : le jeu doit partir de zéro
        run += 1
        write_dir = os.path.join(out, f"{name}-run{run}")
    result = subprocess.run(
        [os.path.join(ROOT, "build", "zeus-run"), "--run", exe, "--headless", "--deterministic",
         "--write-dir", write_dir, "--seconds", str(seconds), "--frame-log", frames_log,
         "--state-log", state_log, "--script", script] + options + extra,
        capture_output=True, text=True)
    lines = result.stderr.strip().splitlines()
    frames = len(read_lines(frames_log)) if os.path.exists(frames_log) else 0
    ticks = sum(1 for line in read_lines(state_log) if not line.startswith("#")) - 1 if os.path.exists(state_log) else 0
    verify = [i for i, line in enumerate(lines) if line.startswith("vérification ")]
    if result.returncode != 0:
        detail = "\n        ".join(lines[verify[0]:verify[0] + 6]) if verify else (lines[-1] if lines else "")
        return f"ÉCHEC   {name} : {detail}", True
    if record:
        return f"référence enregistrée : {name}, {frames} images, {ticks} tours", False
    if not os.path.exists(frames_reference) or not os.path.exists(state_reference):
        return f"ÉCHEC   {name} : pas de référence (lancer d'abord « record »)", True
    problems = []
    if read_lines(frames_reference) != read_lines(frames_log):
        problems.append("journal d'images différent de la référence")
    state = state_difference(state_reference, state_log)
    if state:
        problems.append(state)
    summary = f"{name} : {frames} images, {ticks} tours"
    if verify:
        summary += " ; " + lines[verify[0]].split(" : ", 1)[1]
    return f"{'ÉCHEC  ' if problems else 'ok     '} {summary}" + "".join(", " + p for p in problems), bool(problems)


def main(argv):
    record = "record" in argv
    exe = argv[argv.index("--exe") + 1] if "--exe" in argv else os.path.join(ROOT, "original", "gog", "app", "Zeus.exe")
    extra = ["--no-native"] if "--no-native" in argv or record else []
    if "--verify" in argv:
        extra += ["--verify", argv[argv.index("--verify") + 1]]
        if "--every" in argv:
            extra += ["--verify-every", argv[argv.index("--every") + 1]]
    jobs = int(argv[argv.index("--jobs") + 1]) if "--jobs" in argv else max(1, (os.cpu_count() or 2) - 1)
    out = os.path.join(ROOT, "build", "scenarios")
    os.makedirs(out, exist_ok=True)
    shared = None
    if "--references" in argv:
        key, lines = host_key(exe)
        shared = os.path.join(os.path.abspath(argv[argv.index("--references") + 1]), key)
        os.makedirs(shared, exist_ok=True)
        with open(os.path.join(shared, "cle.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        print(f"références : {shared}", flush=True)
    paths = sorted(glob.glob(os.path.join(ROOT, "tests", "scenarios", "*.txt")))
    if "--only" in argv:
        only = argv[argv.index("--only") + 1].split(",")
        paths = [p for p in paths if os.path.splitext(os.path.basename(p))[0] in only]
    # Les dossiers d'écriture se choisissent avant le lancement : deux scénarios ont des noms
    # distincts, donc des dossiers distincts.
    def task(p):
        if shared:
            recorded = record_shared(p, out, exe, shared, record)
            if record or (recorded and recorded[1]):
                return recorded
            if recorded:
                print(recorded[0], flush=True)
        return play(p, out, exe, extra, record, reference_paths(p, out, shared))

    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(task, p) for p in paths]
        failures = 0
        for future in futures:  # ordre alphabétique, au fur et à mesure
            line, failed = future.result()
            print(line, flush=True)
            failures += failed
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
