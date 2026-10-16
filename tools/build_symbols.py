#!/usr/bin/env python3
"""Construit analysis/symbols.tsv : la carte du code (une ligne par fonction du jeu).

Usage : python3 tools/build_symbols.py CARTE.tsv [analysis/symbols.tsv]

CARTE.tsv est produit par tools/ghidra/ExportCodeMap.java (adresses, tailles, appelés, imports,
chaînes citées). La sortie ne contient que des adresses, des tailles et nos propres noms.

Les noms viennent de analysis/noms.tsv (écrit à la main, chacun avec sa preuve). Le module de
chaque autre fonction est cherché dans cet ordre, du plus sûr au plus fragile :
  1. chaîne citée par la fonction (mots-clés de RÈGLES_CHAÎNES) ;
  2. import appelé directement (MSS32 : son, DDRAW : rendu...) ;
  3. zone de la bibliothèque C (adresses après la première fonction qui cite les messages du VC++) ;
  4. tour de simulation (analysis/simulation.tsv, produit par tools/simulation_map.py) : une
     fonction exécutée pendant le tour prend le module des racines du tour dont elle descend
     (appelées du tour et étapes du cycle, classées à la main dans noms.tsv) quand elles ont
     toutes le même module, ou celui de la grande majorité d'entre elles ; sinon celui de ses
     appelantes pendant le tour, si elles s'accordent de même ; seulement pour les fonctions dont la moitié au moins des blocs
     exécutés le sont pendant le tour ;
  5. hypothèse : vote des appelants et appelés déjà classés.
Les classements des étapes 3 à 5 portent « hypothèse » dans la colonne preuve. Le voisinage dans
l'exécutable a été essayé et écarté : plus de la moitié des cas connus étaient faux.
"""
import collections
import re
import sys

MODULES = ["rendu", "interface", "texte", "terrain", "marcheurs", "bâtiments", "économie",
           "dieux et héros", "armée", "scénario", "sauvegarde", "son", "bibliothèque C", "inconnu"]

# (module, étiquette, motif) : le premier motif trouvé dans une chaîne citée donne l'indice.
RÈGLES_CHAÎNES = [
    ("bibliothèque C", "messages du VC++", r"MSVCRT|Runtime Error|R6009|Microsoft Visual C|1#(SNAN|IND|INF|QNAN)|e\+000|program name unknown|HEAP_SELECT"),
    ("sauvegarde", "fichiers .sav/.pak", r"\.sav\b|\.pak\b|Save\\|parent\.dat|zeus\.dat|COMP Error|PKWARE|Tutorial File"),
    ("scénario", "cartes et campagnes", r"\.map\b|Zeusmap|MapOfGreece|Poseidon_map|campaign|GOAL|Adventures|[Mm]ission|tutorial|Basic tutorial"),
    ("son", "fichiers audio", r"\.wav|\.mp3|[Aa]udio\\|[Ss]ound|[Mm]usic|Phaser|EAX|MUSIC|LAYER\d"),
    ("texte", "fichiers de texte", r"Zeus_Text|Editor_Text|_Text\.eng|dialog\.loc|eventmsg|event message|Zeus_MM|game text|\.eng\b"),
    ("rendu", "affichage et images", r"\bDD\b|DirectDraw|GFX|[Ss]urface|display mode|\.bik\b|[Bb]ink|BINK|\.jpg|\.sg3|Graphic file|Zeus_system|Zeus_Fonts|Zeus_Interface|\.bmp\b"),
    ("dieux et héros", "dieux et monstres", r"\bGOD\b|\bgod\b|[Mm]onster|Poseidon to give|Demeter to give|Dionysus to give|Artemis to give|Sanctuary|[Hh]ero"),
    ("marcheurs", "figures", r"[Ff]igure|Figs\b|route slot|[Ww]alker"),
    ("bâtiments", "bâtiments", r"[Bb]uilding|[Mm]onument|housing|ALL BUILDINGS|Palace"),
    ("économie", "population et impôts", r"[Tt]ax|Pop is|births|deaths|Happiness|spare cap|food|trade"),
    ("armée", "troupes", r"[Cc]ohort|[Aa]rcher|[Aa]rmy|[Ss]oldier|[Cc]ombat"),
    ("terrain", "altitudes", r"[Ee]levation"),
    ("interface", "fenêtre et réglages", r"Window wider|Allow All|Magellan|MSWHEEL|MouseZ|WinSJBClass|[Ii]ni\b|CDDrive|\(F\d\)|/sg=|/map=|setup\.bmp"),
]
IMPORTS = {"MSS32.DLL": ("son", 3), "BINKW32.DLL": ("rendu", 3), "DDRAW.DLL": ("rendu", 3),
           "IJL10.DLL": ("rendu", 3), "GDI32.DLL": ("rendu", 2)}
# USER32, WINMM (horloge de la simulation) et KERNEL32 servent à tous les modules : aucun indice.


def lire_noms(chemin):
    noms = {}
    with open(chemin, encoding="utf-8") as f:
        for ligne in f.read().splitlines()[1:]:
            a, nom, module, statut, preuve, genre = ligne.split("\t")
            noms[int(a, 16)] = (nom, module, statut, preuve, genre)
    return noms


def classer_chaines(chaines):
    votes = collections.Counter()
    exemples = {}
    for chaine in chaines:
        for module, etiquette, motif in RÈGLES_CHAÎNES:
            if re.search(motif, chaine):
                votes[module] += 1
                exemples.setdefault(module, etiquette)
                break
    if not votes:
        return None
    ordre = [m for m, *_ in RÈGLES_CHAÎNES]
    module = max(votes, key=lambda m: (votes[m], -ordre.index(m)))
    return module, f"chaîne ({exemples[module]})"


def lire_tour(chemin):
    """analysis/simulation.tsv : fonctions exécutées pendant le tour, racines dont elles descendent
    et appelantes directes pendant le tour. Celles dont moins de la moitié des blocs exécutés le
    sont pendant le tour sont des outils d'autres modules (textes, images, géométrie de la carte) :
    leur place dans le tour ne dit rien de leur module, elles sont laissées de côté."""
    tour = {}
    try:
        with open(chemin, encoding="utf-8") as f:
            for ligne in f.read().splitlines()[1:]:
                a, racines, appelants, part = (ligne.split("\t") + ["", "", "1"])[:4]
                if float(part) < 0.5:
                    continue
                tour[int(a, 16)] = ([int(x, 16) for x in racines.split(",") if x],
                                    [int(x, 16) for x in appelants.split(",") if x])
    except FileNotFoundError:
        pass
    return tour


def classer_tour(racines, module):
    """Module des racines du tour dont descend une fonction : unanime, ou au moins 3 sur 4."""
    votes = collections.Counter(module[r] for r in racines if module.get(r, "inconnu") != "inconnu")
    if not votes or sum(votes.values()) < len(racines):
        return None
    m, n = votes.most_common(1)[0]
    if n == len(racines):
        if n == 1:
            return m, f"hypothèse : tour de simulation, atteinte seulement depuis {racines[0]:08x} ({m})"
        return m, f"hypothèse : tour de simulation, atteinte depuis {n} racines, toutes en {m}"
    if n * 4 >= len(racines) * 3:
        return m, f"hypothèse : tour de simulation, {n} racines sur {len(racines)} en {m}"
    return None


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    sortie = argv[2] if len(argv) > 2 else "analysis/symbols.tsv"
    noms = lire_noms("analysis/noms.tsv")
    fonctions = {}
    with open(argv[1], encoding="utf-8") as f:
        for ligne in f.read().splitlines()[1:]:
            p = ligne.split("\t")
            p += [""] * (7 - len(p))
            adresse = int(p[0], 16)
            fonctions[adresse] = dict(taille=int(p[1]), instr=int(p[2]),
                                      appeles=[int(x, 16) for x in p[4].split(",") if x],
                                      imports=[x for x in p[5].split(",") if x],
                                      chaines=[x for x in p[6].split("|") if x])
    ordre = sorted(fonctions)
    appelants = collections.defaultdict(list)
    for a, f in fonctions.items():
        for c in f["appeles"]:
            if c in fonctions:
                appelants[c].append(a)

    module, preuve = {}, {}
    for a, (nom, mod, statut, texte, genre) in noms.items():
        if a in fonctions:
            module[a], preuve[a] = mod, texte
    # 1. chaînes ; 2. imports
    for a, f in fonctions.items():
        if a in module:
            continue
        r = classer_chaines(f["chaines"])
        if r:
            module[a], preuve[a] = r
            continue
        votes = collections.Counter()
        for imp in f["imports"]:
            m = IMPORTS.get(imp.split("!")[0])
            if m:
                votes[m[0]] += m[1]
        if votes:
            m = votes.most_common(1)[0][0]
            module[a], preuve[a] = m, "import (" + next(i.split("!")[0] for i in f["imports"] if IMPORTS.get(i.split("!")[0], ("",))[0] == m) + ")"
    # 3. zone de la bibliothèque C : à partir de la première fonction qui cite les messages du VC++
    debut_crt = min((a for a in module if preuve[a] == "chaîne (messages du VC++)"), default=None)
    if debut_crt is not None:
        for a in ordre:
            if a >= debut_crt and a not in module:
                module[a] = "bibliothèque C"
                preuve[a] = f"hypothèse : zone de la bibliothèque C (après {debut_crt:08x})"
    # 4. tour de simulation : racines classées à la main (noms.tsv), puis, pour les fonctions
    # partagées entre racines de modules différents, appelantes directes pendant le tour
    tour = lire_tour("analysis/simulation.tsv")
    for a in ordre:
        if a not in module and a in tour and tour[a][0]:
            r = classer_tour(tour[a][0], module)
            if r:
                module[a], preuve[a] = r
    # Puis appelantes directes pendant le tour, 3 sur 4 au moins dans un module. Le module le plus
    # fréquent parmi les appelantes a été essayé pour les fonctions partagées et écarté : sur les
    # fonctions nommées cachées, il se trompait deux fois sur trois (docs/engine/simulation.md §7).
    for ronde in range(8):
        nouveaux = {}
        for a in ordre:
            if a in module or a not in tour:
                continue
            votes = collections.Counter(module[c] for c in tour[a][1] if module.get(c, "inconnu") != "inconnu")
            total = len(tour[a][1])
            if not votes:
                continue
            m, n = votes.most_common(1)[0]
            if sum(votes.values()) == total and n * 4 >= total * 3:
                nouveaux[a] = (m, f"hypothèse : tour de simulation, appelée par {n} fonctions sur {total} en {m}")
        if not nouveaux:
            break
        for a, (m, t) in nouveaux.items():
            module[a], preuve[a] = m, t
    # 5. vote des appelants (poids 2, si la fonction a peu d'appelants) et des appelés (poids 1,
    # hors fonctions utilitaires appelées de partout)
    forts = set(module)
    for ronde in range(4):
        nouveaux = {}
        for a in ordre:
            if a in module:
                continue
            f = fonctions[a]
            votes = collections.Counter()
            if len(appelants[a]) <= 6:
                for x in appelants[a]:
                    if x in module and module[x] != "inconnu" and (ronde > 0 or x in forts):
                        votes[module[x]] += 2
            for x in f["appeles"]:
                if x in module and module[x] != "inconnu" and len(appelants[x]) <= 40 and (ronde > 0 or x in forts):
                    votes[module[x]] += 1
            total = sum(votes.values())
            if total < 3:
                continue
            m, n = votes.most_common(1)[0]
            if n / total >= 0.75:
                nouveaux[a] = (m, f"hypothèse : appelants/appelés (poids {n} sur {total} en {m})")
        if not nouveaux:
            break
        for a, (m, t) in nouveaux.items():
            module[a], preuve[a] = m, t
    lignes = []
    adresses = set(fonctions) | set(noms)
    for a in sorted(adresses):
        f = fonctions.get(a)
        if a in noms:
            nom, mod, statut, texte, genre = noms[a]
            taille = f["taille"] if f else 0
        else:
            nom, genre, statut = f"FUN_{a:08x}", "fonction", "inconnu"
            mod = module.get(a, "inconnu")
            texte = preuve.get(a, "aucun indice")
            taille = f["taille"]
        lignes.append((f"{a:08x}", str(taille), nom, mod, statut, texte, genre))
    with open(sortie, "w", encoding="utf-8") as out:
        out.write("adresse\ttaille\tnom\tmodule\tstatut\tpreuve\ttype\n")
        for l in lignes:
            out.write("\t".join(l) + "\n")
    par_module = collections.Counter(l[3] for l in lignes if l[6] == "fonction")
    print(f"{sum(par_module.values())} fonctions, {len(lignes) - sum(par_module.values())} données")
    for m in MODULES:
        print(f"  {m:16s} {par_module[m]}")


if __name__ == "__main__":
    main(sys.argv)
