#!/usr/bin/env python3
"""Résume un profil de zeus-run --profile par fonction.

Usage : python3 tools/profile_report.py PROFIL.tsv FONCTIONS.tsv [NOMBRE]

FONCTIONS.tsv est la liste produite par tools/ghidra/ExportFunctions.java.
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
    total = 0
    per_function = {}
    with open(argv[1]) as f:
        for line in f:
            address, time = line.split()
            address, time = int(address, 16), int(time)
            total += time
            i = bisect.bisect_right(starts, address) - 1
            key = functions[i] if i >= 0 else (address, 0, "?")
            per_function[key] = per_function.get(key, 0) + time
    ranked = sorted(per_function.items(), key=lambda item: -item[1])
    print(f"temps total : {total / 1e9:.1f} s, {len(ranked)} fonctions")
    cumulated = 0
    for rank, ((address, size, name), time) in enumerate(ranked[:count], 1):
        cumulated += time
        print(f"{rank:3d}  {address:08x}  {size:6d} octets  {100 * time / total:5.1f} %  cumul {100 * cumulated / total:5.1f} %  {name}")
    for n in (10, 25, 50, 100, 200):
        print(f"les {n} premières : {100 * sum(t for _, t in ranked[:n]) / total:.1f} %")


if __name__ == "__main__":
    main(sys.argv)
