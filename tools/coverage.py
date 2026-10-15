#!/usr/bin/env python3
"""Compteurs d'avancement du moteur : fonctions par statut et par module, part exécutée en natif.

Usage : python3 tools/coverage.py [--symbols analysis/symbols.tsv] [--profile PROFIL.tsv]
                                  [--markdown | --summary]

Fonctions : lues dans analysis/symbols.tsv (lignes de type « fonction »). Statuts : inconnu,
nommé, documenté, natif. Une fonction « nommé » dont la preuve commence par « natif partiel »
est comptée à part (chemin rapide ou boucle réécrite, le reste du code d'origine tournant encore).

Part en natif : à partir d'un profil de `zeus-run --no-native --profile PROFIL.tsv` (le jeu
entièrement émulé), deux mesures sur le code du jeu seul (les blocs hors du .text, c'est-à-dire
le travail de l'hôte appelé par le jeu, sont exclus) :
  - le temps de calcul : temps passé dans les fonctions de statut « natif », corrigé du coût de
    la mesure, sur le temps passé dans le code x86 du jeu ;
  - les octets de code exécutés (blocs × taille) : pas un nombre d'instructions exact, et une
    instruction répétée (rep movs) y compte pour une.
L'hôte (Win32, blits, son) est donné à part, en part du temps mesuré. Les profils à trois colonnes
(sans la taille des blocs) ne conviennent pas.
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
    """Mesures d'un profil, pour le code du jeu seul (les blocs hors du .text sont ceux de l'hôte).

    Renvoie un dict : pour « octets » et « temps », les parts (natif, partiel) du code du jeu, et
    la part de l'hôte dans le temps mesuré. Le temps est corrigé du coût de la mesure, comme dans
    profile_report.py (temps moyen le plus court des blocs exécutés plus de 100 000 fois).
    """
    debuts = [f[0] for f in fonctions]
    fin = max(f[0] + f[1] for f in fonctions if f[1] > 0)
    blocs = []
    for ligne in open(chemin):
        champs = ligne.split()
        if len(champs) < 4:
            raise SystemExit("profil sans taille de bloc : à refaire avec un zeus-run récent")
        blocs.append((int(champs[0], 16), int(champs[1]), int(champs[2]), int(champs[3])))
    chargees = [t / e for _, t, e, _ in blocs if e >= 100000]
    surcout = min(chargees) if chargees else 0.0
    mesure = collections.defaultdict(float)
    for adresse, nanos, executions, taille in blocs:
        temps = max(nanos - surcout * executions, 0.0)
        if adresse >= fin or adresse < debuts[0]:
            mesure["hôte"] += temps
            continue
        i = bisect.bisect_right(debuts, adresse) - 1
        _, _, _, _, statut, preuve = fonctions[i]
        genre = "natif" if statut == "natif" else "partiel" if "natif partiel" in preuve else "reste"
        mesure["octets", genre] += executions * taille
        mesure["temps", genre] += temps
    jeu_octets = sum(mesure["octets", g] for g in ("natif", "partiel", "reste"))
    jeu_temps = sum(mesure["temps", g] for g in ("natif", "partiel", "reste"))
    return {"octets_natif": mesure["octets", "natif"] / jeu_octets,
            "octets_partiel": mesure["octets", "partiel"] / jeu_octets,
            "temps_natif": mesure["temps", "natif"] / jeu_temps,
            "temps_partiel": mesure["temps", "partiel"] / jeu_temps,
            "hote": mesure["hôte"] / (jeu_temps + mesure["hôte"])}


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
        m = part_native(fonctions, a.profile)
        texte_natif = (f"{100 * m['temps_natif']:.1f} % du temps de calcul du jeu dans des fonctions natives "
                       f"(+ {100 * m['temps_partiel']:.1f} % partiel) ; {100 * m['octets_natif']:.1f} % des octets de "
                       f"code exécutés (+ {100 * m['octets_partiel']:.1f} % partiel) ; l'hôte pèse {100 * m['hote']:.1f} % "
                       f"du temps mesuré")
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
