#!/usr/bin/env python3
"""Compteurs d'avancement du moteur : fonctions par statut et par module, part exécutée en natif.

Usage : python3 tools/coverage.py [--symbols analysis/symbols.tsv] [--profile PROFIL.tsv]
                                  [--markdown | --summary]

Fonctions : lues dans analysis/symbols.tsv (lignes de type « fonction »). Statuts : inconnu,
nommé, documenté, natif. Une fonction « nommé » dont la preuve commence par « natif partiel »
est comptée à part (chemin rapide ou boucle réécrite, le reste du code d'origine tournant encore).

Part en natif : à partir d'un profil de `zeus-run --no-native --profile PROFIL.tsv` (le jeu
entièrement émulé), la part des octets de code exécutés qui appartiennent à des fonctions de
statut « natif ». C'est une mesure d'octets d'instructions, pas un nombre d'instructions : les
longueurs d'instructions x86 varient, mais la part est la même à longueur moyenne égale. Les
anciennes sorties de profil à trois colonnes (sans la taille des blocs) ne conviennent pas.
--summary n'imprime que les deux compteurs, en une ligne ; --markdown, les tableaux de
docs/engine/README.md.
"""
import argparse
import bisect
import collections

STATUTS = ["inconnu", "nommé", "documenté", "natif"]
MODULES = ["rendu", "interface", "texte", "terrain", "marcheurs", "bâtiments", "économie",
           "dieux et héros", "armée", "scénario", "sauvegarde", "son", "bibliothèque C", "inconnu"]


def lire_symboles(chemin):
    fonctions = []
    with open(chemin, encoding="utf-8") as f:
        for ligne in f.read().splitlines()[1:]:
            a, taille, nom, module, statut, preuve, genre = ligne.split("\t")
            if genre == "fonction":
                fonctions.append((int(a, 16), int(taille), nom, module, statut, preuve))
    fonctions.sort()
    return fonctions


def part_native(fonctions, chemin):
    """(octets natifs, octets partiels, octets exécutés, par module) d'après un profil."""
    debuts = [f[0] for f in fonctions]
    natif = partiel = total = 0
    for ligne in open(chemin):
        champs = ligne.split()
        if len(champs) < 4:
            raise SystemExit("profil sans taille de bloc : à refaire avec un zeus-run récent")
        adresse, executions, taille = int(champs[0], 16), int(champs[2]), int(champs[3])
        octets = executions * taille
        total += octets
        i = bisect.bisect_right(debuts, adresse) - 1
        if i < 0:
            continue
        _, _, _, _, statut, preuve = fonctions[i]
        if statut == "natif":
            natif += octets
        elif preuve.startswith("natif partiel") or "natif partiel" in preuve:
            partiel += octets
    return natif, partiel, total


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--symbols", default="analysis/symbols.tsv")
    ap.add_argument("--profile")
    ap.add_argument("--markdown", action="store_true")
    ap.add_argument("--summary", action="store_true")
    a = ap.parse_args()
    fonctions = lire_symboles(a.symbols)
    par_statut = collections.Counter(f[4] for f in fonctions)
    partiels = sum(1 for f in fonctions if f[4] == "nommé" and "natif partiel" in f[5])
    table = {m: collections.Counter() for m in MODULES}
    for f in fonctions:
        table[f[3]][f[4]] += 1
    natives = par_statut["natif"]
    texte_natif = ""
    if a.profile:
        natif, partiel, total = part_native(fonctions, a.profile)
        texte_natif = f"{100 * natif / total:.1f} % du code exécuté en natif (+ {100 * partiel / total:.1f} % partiel)"
    if a.summary:
        print(f"{natives} fonctions natives sur {len(fonctions)}" + (f" ; {texte_natif}" if texte_natif else ""))
        return
    if a.markdown:
        print("| Module | Fonctions | Inconnu | Nommé | Documenté | Natif |")
        print("|---|---:|---:|---:|---:|---:|")
        for m in MODULES:
            c = table[m]
            print(f"| {m} | {sum(c.values())} | " + " | ".join(str(c[s]) for s in STATUTS) + " |")
        print(f"| **Total** | **{len(fonctions)}** | " + " | ".join(f"**{par_statut[s]}**" for s in STATUTS) + " |")
        return
    print(f"{len(fonctions)} fonctions : " + ", ".join(f"{par_statut[s]} {s}" for s in STATUTS)
          + f" (dont {partiels} nommées en natif partiel)")
    for m in MODULES:
        c = table[m]
        print(f"  {m:16s} {sum(c.values()):5d}  " + "  ".join(f"{s} {c[s]}" for s in STATUTS))
    if texte_natif:
        print(texte_natif)


if __name__ == "__main__":
    main()
