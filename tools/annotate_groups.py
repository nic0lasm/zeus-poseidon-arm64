#!/usr/bin/env python3
"""Annote un fichier décompilé : remplace image_id_from_group(N) par le nom du groupe de sprites.

Un argument N vaut collection * 512 + rang du groupe (à partir de 1, groupes vides exclus) ; le
nom vient de la table des groupes du fichier .sg3 de la collection. L'annotation donne
« fichier:groupe@numéro », le numéro étant celui de la première image dans la collection.

Usage : annotate_groups.py ENTRÉE.c SORTIE.c [DOSSIER_DATA]
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sg3  # noqa: E402

COLLECTIONS = {0: "Zeus_Fonts", 1: "Zeus_Interface", 2: "Zeus_Unloaded", 3: "Poseidon_Unloaded",
               4: "Zeus_General", 5: "Zeus_Terrain", 16: "SprAmbient", 17: "Destruction",
               51: "SprMain", 52: "Poseidon_Loaded", 53: "PoseidonImps"}


def group_ranks(sg, collection):
    """Groupes dans l'ordre de leur rang : le jeu ne garde que les groupes non vides.

    Le chargeur (0x00641DF4) parcourt les 300 groupes et n'enregistre que ceux dont la première
    image dépasse les images système ; le rang passé à image_id_from_group compte ces seuls
    groupes. Vérifié : le rang 10 de Zeus_Terrain sert de remplissage hors carte avec un modulo
    58, et SPR_IN_FILL1, 10e groupe non vide, a exactement 58 images.
    """
    skip = 0 if collection == 0 else 200
    return [(tag, first - skip) for tag, first in zip(sg.tags, sg.groups) if first - skip > 0]


def main(argv):
    if len(argv) not in (3, 4):
        sys.exit(__doc__)
    data = argv[3] if len(argv) == 4 else os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "original", "install", "DATA")
    real = {n.lower(): n for n in os.listdir(data)}
    cache = {}

    def name(value):
        collection, rank = divmod(value, 512)
        if collection not in COLLECTIONS or rank == 0:
            return None
        if collection not in cache:
            cache[collection] = group_ranks(
                sg3.SgFile(os.path.join(data, real[COLLECTIONS[collection].lower() + ".sg3"])), collection)
        ranks = cache[collection]
        if rank > len(ranks):
            return None
        tag, first = ranks[rank - 1]
        return f"{COLLECTIONS[collection]}:{tag}@{first}"

    def repl(m):
        value = int(m.group(1), 16) if m.group(1).startswith("0x") else int(m.group(1))
        label = name(value)
        return m.group(0) if label is None else f"image_id_from_group(/*{label}*/{m.group(1)})"

    with open(argv[1], encoding="utf-8") as fh:
        text = fh.read()
    text, count = re.subn(r"image_id_from_group\((0x[0-9a-fA-F]+|\d+)\)", repl, text)
    with open(argv[2], "w", encoding="utf-8") as fh:
        fh.write(text)
    print(f"{count} appels examinés")


if __name__ == "__main__":
    main(sys.argv)
