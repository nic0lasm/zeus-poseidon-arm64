#!/usr/bin/env python3
"""Carte de la simulation : arbre d'appels du tour de simulation et coût de chaque module.

Usage : python3 tools/simulation_map.py record [--exe ZEUS.EXE] [--jobs N] [--only NOM,…]
        python3 tools/simulation_map.py report [--symbols analysis/symbols.tsv] [--table SORTIE.tsv]

  record  joue les scénarios de tests/scenarios avec zeus-run --call-graph, code d'origine seul
          (--no-native), plus `working-city` avec les remplacements natifs actifs ; écrit
          build/simulation/NOM.tsv (et NOM-natif.tsv)
  report  résume ces fichiers avec la carte du code : fonctions atteintes par le tour, modules,
          coût du tour et de chacune de ses branches

Les enregistrements se font sous horloge fictive (--deterministic), comme le banc : deux
exécutions donnent le même arbre. Les temps sont ceux de l'émulateur, corrigés du coût de la
mesure comme dans tools/profile_report.py.
"""
import argparse
import bisect
import collections
import concurrent.futures
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from check_scenarios import read_scenario  # noqa: E402

OUT = os.path.join(ROOT, "build", "simulation")
TICK = 0x00513A30


def play(path, exe, native):
    name = os.path.splitext(os.path.basename(path))[0] + ("-natif" if native else "")
    script, seconds, options = read_scenario(path)
    graph = os.path.join(OUT, name + ".tsv")
    run = 0
    write_dir = os.path.join(OUT, f"{name}-run{run}")
    while os.path.exists(write_dir):  # dossier d'écriture neuf : le jeu doit partir de zéro
        run += 1
        write_dir = os.path.join(OUT, f"{name}-run{run}")
    result = subprocess.run(
        [os.path.join(ROOT, "build", "zeus-run"), "--run", exe, "--headless", "--deterministic",
         "--write-dir", write_dir, "--seconds", str(seconds), "--call-graph", graph, "--script", script]
        + options + ([] if native else ["--no-native"]),
        capture_output=True, text=True)
    lines = result.stderr.strip().splitlines()
    if result.returncode != 0:
        return f"ÉCHEC   {name} : {lines[-1] if lines else ''}"
    with open(graph, encoding="utf-8") as f:
        return f"ok      {name} : {f.readline().split(' : ', 1)[1].strip()}"


def record(args):
    os.makedirs(OUT, exist_ok=True)
    paths = sorted(glob.glob(os.path.join(ROOT, "tests", "scenarios", "*.txt")))
    if args.only:
        wanted = set(args.only.split(","))
        paths = [p for p in paths if os.path.splitext(os.path.basename(p))[0] in wanted]
    jobs = [(p, False) for p in paths] + [(p, True) for p in paths if p.endswith("working-city.txt")]
    # Le plus long d'abord : working-city dure 300 s de jeu.
    jobs.sort(key=lambda j: -read_scenario(j[0])[1])
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for line in pool.map(lambda j: play(j[0], args.exe, j[1]), jobs):
            print(line, flush=True)


# --- Lecture ---------------------------------------------------------------------------------

def read_symbols(path):
    functions = []
    with open(path, encoding="utf-8") as f:
        for line in f.read().splitlines()[1:]:
            a, size, name, module, status, evidence, kind = line.split("\t")
            if kind == "fonction":
                functions.append((int(a, 16), int(size), name, module, status, evidence))
    functions.sort()
    return functions


class Graph:
    """Un fichier de zeus-run --call-graph."""

    def __init__(self, path):
        self.blocks = {}      # adresse -> (octets, exécutions [hors, tour], ns [hors, tour])
        self.calls = collections.Counter()   # (site, appelée, pendant le tour) -> appels
        self.functions = {}   # adresse -> (appels, ns inclus, blocs inclus)
        self.ticks = 0
        with open(path, encoding="utf-8") as f:
            for line in f:
                if line.startswith("#"):
                    if "tours de simulation" in line:
                        self.ticks = int(line.split(" : ")[1].split()[0])
                    continue
                p = line.split("\t")
                if p[0] == "bloc":
                    self.blocks[int(p[1], 16)] = (int(p[2]), (int(p[3]), int(p[5])), (int(p[4]), int(p[6])))
                elif p[0] == "appel":
                    site, callee = int(p[1], 16), int(p[2], 16)
                    if int(p[3]):
                        self.calls[site, callee, False] += int(p[3])
                    if int(p[4]):
                        self.calls[site, callee, True] += int(p[4])
                elif p[0] == "fonction":
                    self.functions[int(p[1], 16)] = (int(p[2]), int(p[3]), int(p[4]))

    def overhead(self):
        """Coût de la mesure par bloc : temps moyen le plus court des blocs très exécutés."""
        busy = [sum(ns) / sum(ex) for _, ex, ns in self.blocks.values() if sum(ex) >= 100000]
        return min(busy) if busy else 0.0


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    r = sub.add_parser("record")
    r.add_argument("--exe", default=os.path.join(ROOT, "original", "gog", "app", "Zeus.exe"))
    r.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    r.add_argument("--only")
    s = sub.add_parser("report")
    s.add_argument("--symbols", default=os.path.join(ROOT, "analysis", "symbols.tsv"))
    s.add_argument("--table", help="écrit la liste des fonctions du tour et de leurs racines (analysis/simulation.tsv)")
    args = parser.parse_args(argv[1:])
    if args.command == "record":
        record(args)
    else:
        report(args)


CYCLE = 0x00483840
CITY_DRAW = 0x00458170


class CodeMap:
    """analysis/symbols.tsv : rattache une adresse de code à sa fonction."""

    def __init__(self, path):
        self.functions = read_symbols(path)
        self.starts = [f[0] for f in self.functions]
        self.by_address = {f[0]: f for f in self.functions}
        self.end = max(f[0] + f[1] for f in self.functions)

    def function(self, address):
        """Début de la fonction qui contient `address`, ou None hors du code du jeu."""
        if address < self.starts[0] or address >= self.end:
            return None
        return self.starts[bisect.bisect_right(self.starts, address) - 1]

    def module(self, address):
        return self.by_address[address][3] if address in self.by_address else "inconnu"


def load(code):
    """Les fichiers de build/simulation, par scénario."""
    graphs = {}
    for path in sorted(glob.glob(os.path.join(OUT, "*.tsv"))):
        graphs[os.path.splitext(os.path.basename(path))[0]] = Graph(path)
    if not graphs:
        raise SystemExit("aucun arbre d'appels dans build/simulation : lancer d'abord « record »")
    return graphs


def tick_tree(graph, code):
    """Graphe des appels faits pendant le tour, d'une fonction à l'autre : {appelante: {appelée: appels}}."""
    edges = collections.defaultdict(collections.Counter)
    for (site, callee, in_tick), n in graph.calls.items():
        caller = code.function(site)
        if in_tick and caller is not None and code.function(callee) is not None:
            edges[caller][code.function(callee)] += n
    return edges


def roots_of(edges, executed):
    """Racines des branches du tour : ses appelées directes, le cycle de 51 étapes remplacé par les
    siennes, et les fonctions exécutées pendant le tour sans y être appelées, où l'on entre par un
    saut (le tour finit par « jmp » vers walkers_update, le cycle vers 0x00483C10)."""
    called = {g for f in edges for g in edges[f]}
    roots = [f for f in edges[TICK] if f != CYCLE] + list(edges[CYCLE])
    roots += sorted(f for f in executed if f not in called and f not in (TICK, CYCLE) and f not in roots)
    return roots


def reach(edges, roots):
    """Pour chaque fonction atteinte pendant le tour, les racines dont elle descend."""
    reached = collections.defaultdict(set)
    for root in roots:
        stack, seen = [root], {root, TICK, CYCLE}
        while stack:
            f = stack.pop()
            reached[f].add(root)
            for g in edges.get(f, ()):
                if g not in seen:
                    seen.add(g)
                    stack.append(g)
    return reached


def self_time(graph, code, overhead):
    """Temps propre (ns corrigés) par fonction : [hors du tour, pendant le tour], code du jeu seul."""
    times = collections.defaultdict(lambda: [0.0, 0.0])
    for address, (size, executions, ns) in graph.blocks.items():
        f = code.function(address)
        if f is None:
            continue
        for k in (0, 1):
            times[f][k] += max(ns[k] - overhead * executions[k], 0.0)
    return times


def inclusive(graph, address, overhead):
    calls, ns, blocks = graph.functions.get(address, (0, 0, 0))
    return calls, max(ns - overhead * blocks, 0.0)


def report(args):
    code = CodeMap(args.symbols)
    graphs = load(code)
    text_bytes = sum(f[1] for f in code.functions)
    print("Scénarios (code d'origine seul sauf -natif ; temps corrigés du coût de la mesure)")
    print(f"{'scénario':24s} {'tours':>6s} {'jeu (s)':>8s} {'tour':>6s} {'dessin':>7s} {'ms/tour':>8s}")
    union = collections.defaultdict(set)
    callers = collections.defaultdict(set)
    executions = collections.defaultdict(lambda: [0, 0])  # blocs exécutés hors du tour, pendant
    tick_self = collections.defaultdict(float)
    for name, g in graphs.items():
        ov = g.overhead()
        times = self_time(g, code, ov)
        game = sum(a + b for a, b in times.values())
        _, tick = inclusive(g, TICK, ov)
        _, draw = inclusive(g, CITY_DRAW, ov)
        per = tick / g.ticks / 1e6 if g.ticks else 0.0
        print(f"{name:24s} {g.ticks:6d} {game / 1e9:8.1f} {100 * tick / game:5.1f}% {100 * draw / game:6.1f}% {per:8.3f}")
        if name.endswith("-natif"):
            continue
        edges = tick_tree(g, code)
        executed = {f for f, (_, t) in times.items() if t > 0}
        for f, roots in reach(edges, roots_of(edges, executed)).items():
            union[f] |= roots
        for f, callees in edges.items():
            for callee in callees:
                callers[callee].add(f)
        for address, (_, ex, _) in g.blocks.items():
            f = code.function(address)
            if f is not None:
                executions[f][0] += ex[0]
                executions[f][1] += ex[1]
        for f in executed:
            tick_self[f] += times[f][1]
    union.pop(TICK, None)
    union.pop(CYCLE, None)
    reached = set(union) | {TICK, CYCLE}
    by_module = collections.Counter(code.module(f) for f in reached)
    size = sum(code.by_address[f][1] for f in reached if f in code.by_address)
    print(f"\nfonctions exécutées pendant le tour : {len(reached)}, {size} octets "
          f"({100 * size / text_bytes:.1f} % du code des fonctions)")
    total = sum(tick_self.values())
    cost = collections.Counter()
    for f, t in tick_self.items():
        cost[code.module(f)] += t
    for m, n in sorted(by_module.items(), key=lambda x: -cost[x[0]]):
        print(f"  {m:16s} {n:5d} fonctions  {100 * cost[m] / total:5.1f} % du temps du tour")
    if args.table:
        with open(args.table, "w", encoding="utf-8") as out:
            out.write("adresse\tracines\tappelants\tpart_tour\n")
            for f in sorted(reached):
                out_, in_ = executions[f]
                out.write(f"{f:08x}\t{','.join(f'{r:08x}' for r in sorted(union.get(f, ())))}"
                          f"\t{','.join(f'{c:08x}' for c in sorted(callers.get(f, ())))}"
                          f"\t{in_ / (in_ + out_) if in_ + out_ else 0:.2f}\n")


if __name__ == "__main__":
    main(sys.argv)
