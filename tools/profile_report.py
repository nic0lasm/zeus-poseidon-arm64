#!/usr/bin/env python3
"""Résume un profil de zeus-run --profile par fonction.

Usage : python3 tools/profile_report.py PROFIL.tsv FONCTIONS.tsv [NOMBRE]

FONCTIONS.tsv est la liste produite par tools/ghidra/ExportFunctions.java.

La mesure coûte un temps à peu près fixe à chaque exécution d'un bloc. Ce coût est estimé par le
temps moyen le plus court observé parmi les blocs très souvent exécutés, puis retranché :
sans cela, les boucles courtes paraissent bien plus lourdes qu'elles ne sont.
"""
import bisect
import sys


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    count = int(argv[3]) if len(argv) > 3 else 25
    functions = []
    with open(argv[2], encoding="utf-8") as f:
        for line in f.read().splitlines()[1:]:
            address, size, name = line.split("\t")[:3]
            functions.append((int(address, 16), int(size), name))
    functions.sort()
    starts = [f[0] for f in functions]
    blocks = []
    with open(argv[1]) as f:
        for line in f:
            address, time, executions = line.split()
            blocks.append((int(address, 16), int(time), int(executions)))
    busy = [time / executions for _, time, executions in blocks if executions >= 100000]
    overhead = min(busy) if busy else 0.0
    total = raw_total = 0.0
    per_function = {}
    for address, time, executions in blocks:
        raw_total += time
        corrected = max(time - overhead * executions, 0.0)
        total += corrected
        i = bisect.bisect_right(starts, address) - 1
        key = functions[i] if i >= 0 else (address, 0, "?")
        entry = per_function.setdefault(key, [0.0, 0])
        entry[0] += corrected
        entry[1] += executions
    ranked = sorted(per_function.items(), key=lambda item: -item[1][0])
    print(f"temps mesuré : {raw_total / 1e9:.1f} s ; coût de la mesure : {overhead:.0f} ns par bloc ; "
          f"temps corrigé : {total / 1e9:.1f} s ; {len(ranked)} fonctions")
    cumulated = 0.0
    for rank, ((address, size, name), (time, executions)) in enumerate(ranked[:count], 1):
        cumulated += time
        print(f"{rank:3d}  {address:08x}  {size:6d} octets  {100 * time / total:5.1f} %  cumul {100 * cumulated / total:5.1f} %"
              f"  {executions:>11d} blocs  {name}")
    for n in (10, 25, 50, 100, 200):
        print(f"les {n} premières : {100 * sum(t for _, (t, _) in ranked[:n]) / total:.1f} %")


if __name__ == "__main__":
    main(sys.argv)
