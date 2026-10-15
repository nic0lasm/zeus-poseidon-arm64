#!/usr/bin/env python3
"""Lecture et réécriture à l'octet près des aventures en cours d'édition (`.set`, voir
formats/set.md). Un `.set` est aussi l'en-tête de chaque `.pak` (voir pakfile.py).

Usage :
    setfile.py info  FICHIER            champs de l'en-tête, avec les entiers et le début des textes
    setfile.py check CHEMIN...          relit chaque .set (fichiers ou dossiers), le réécrit et
                                        compare à l'original, octet par octet

Le fichier est une suite de champs bruts écrits par fwrite (`0x00425910`) depuis l'objet
aventure ; la lecture (`0x00425D00`) saute ou adapte certains champs selon la version.
"""
import os
import re
import struct
import sys

MAGIC = 0x22A7       # constante écrite en tête par 0x00425910
CURRENT_VERSION = 26  # 0x1A, mise par 0x00426880 avant l'écriture
EPISODES = 10        # épisodes de la ville mère
COLONIES = 4         # épisodes de colonie

# (nom, taille, décalage dans l'objet aventure, version minimale, contenu)
# La taille des champs « mission_extra » dépend de la version (voir fields()).
_FIELDS = [
    ("magic", 4, None, 0, "0x22A7"),
    ("version", 4, 0x4, 0, "version du format : 5 ou 26 dans les fichiers livrés"),
    ("episodes", 4, 0x8, 2, "nombre d'épisodes de la ville mère (hypothèse)"),
    ("value_b", 4, 0xC, 2, "entier"),
    ("colonies", 4, 0x10, 2, "nombre de colonies (hypothèse)"),
    ("episode_texts", 0xDE8, 0x14, 2, "10 × 356 octets : présentation des épisodes de la ville mère"),
    ("colony_texts", 0xDE8, 0xDFC, 2, "10 × 356 octets, 4 utilisés : épisodes de colonie"),
    ("episode_scenarios", 0x4F60, 0x1BE4, 2, "10 × 2 032 octets : description de scénario par épisode (rang 9 des sauvegardes)"),
    ("colony_scenarios", 0x1FC0, 0x6B44, 2, "4 × 2 032 octets : même chose pour les colonies"),
    ("value_h", 4, 0x8B04, 2, "entier"),
    ("value_i", 4, 0x8B08, 2, "entier"),
    ("table_j", 0x10, 0x8B0C, 2, "16 octets"),
] + [(f"value_k{i}", 4, 0x8B1C + 4 * i, 2, "entier") for i in range(11)] + [
    ("table_l", 0x28, 0x8B48, 2, "10 entiers"),
    ("table_m", 0x28, 0x8B70, 2, "10 entiers"),
    ("episode_extra", None, 0x8B98, 2, "10 × 300 octets (10 × 224 avant la version 23) : rang 122 des sauvegardes"),
    ("colony_extra", None, 0x9750, 2, "4 × 300 octets (4 × 224 avant la version 23)"),
    ("episode_events", 0x2D690, 0x9C00, 2, "10 × 18 600 octets : 150 événements de 124 octets par épisode (rang 13 des sauvegardes)"),
    ("colony_events", 0x122A0, 0x37290, 2, "4 × 18 600 octets : événements des colonies"),
    ("kind", 1, 0x49530, 2, "genre : 0 campagne Zeus, 1 et 4 aventure rangée dans son propre dossier, 2 didacticiel, 3 campagne Poséidon"),
    ("value_s", 4, 0x49534, 2, "entier, 0 partout"),
    ("map_offsets", 0x14, 0x49538, 2, "5 décalages dans le .pak : carte de la ville mère puis colonies"),
    ("parent_map", 500000, 0x4954C, 2, "conteneur MAPS complété de zéros (carte de la ville mère, hypothèse)"),
    ("episode_event_counts", 0x28, 0xC366C, 2, "10 entiers : nombre d'événements de chaque épisode"),
    ("colony_event_counts", 0x10, 0xC3694, 2, "4 entiers : nombre d'événements de chaque colonie"),
    ("table_x", 0x280, 0xC36A4, 2, "640 octets"),
    ("table_y", 0x2800, 0xC3924, 2, "10 240 octets"),
    ("table_z", 0x2800, 0xC6124, 2, "10 240 octets"),
    ("table_A", 0x100, 0xC8924, 2, "256 octets"),
    ("table_B", 0x1000, 0xC8A24, 2, "4 096 octets"),
    ("table_C", 0x1000, 0xC9A24, 2, "4 096 octets"),
    ("table_D", 0x40, 0xCAA24, 2, "64 octets"),
    ("table_E", 0x400, 0xCAA64, 2, "1 024 octets"),
    ("table_F", 0x400, 0xCAE64, 2, "1 024 octets"),
    ("table_G", 0x28, 0xCB264, 2, "10 entiers, 50 par défaut"),
    ("table_H", 0x10, 0xCB28C, 2, "4 entiers"),
    ("table_I", 0x1000, 0xCB29C, 2, "4 096 octets"),
    ("value_J", 4, 0xCC29C, 3, "entier"),
    ("table_K", 0x6E, 0xCC2A0, 4, "110 octets"),
    ("table_L", 0x6E, 0xCC30E, 4, "110 octets"),
    ("table_M", 10, 0xCC37C, 5, "10 octets"),
    ("value_N", 4, 0xCC386, 5, "entier"),
    ("value_O", 4, 0xCC7D8, 21, "entier"),
    # suite écrite par 0x0052DEF0 et lue par 0x0052DF50 : variables globales
    ("world_1", 0x10, 0xFE3B5C, 2, "16 octets, variable globale 0x00FE3B5C"),
    ("world_2", 0x720, 0xFE3B6C, 2, "1 824 octets, variable globale 0x00FE3B6C"),
    ("world_3", 0x28, 0xFE2964, 2, "40 octets, variable globale 0x00FE2964"),
    ("world_4", 0x11D0, 0xFE298C, 2, "4 560 octets, variable globale 0x00FE298C"),
]


class FormatError(Exception):
    pass


def fields(version):
    """Liste (nom, taille, décalage objet, contenu) des champs présents dans un fichier de cette
    version, dans l'ordre du fichier."""
    if version <= 1:
        raise FormatError(f"version {version} : ancienne forme (objet entier de 0xCB28C octets), non gérée")
    out = []
    for name, size, offset, since, text in _FIELDS:
        if version < since:
            continue
        if size is None:  # 300 octets par épisode depuis la version 23, 224 avant
            size = (EPISODES if name == "episode_extra" else COLONIES) * (300 if version >= 23 else 224)
        out.append((name, size, offset, text))
    return out


class SetHeader:
    """En-tête d'aventure : `values` associe à chaque champ ses octets bruts."""

    def __init__(self, data, start=0):
        self.magic, self.version = struct.unpack_from("<II", data, start)
        if self.magic != MAGIC:
            raise FormatError(f"signature {self.magic:#x} au lieu de {MAGIC:#x}")
        self.fields = fields(self.version)
        self.values = {}
        pos = start
        for name, size, _, _ in self.fields:
            if pos + size > len(data):
                raise FormatError(f"champ {name} hors du fichier")
            self.values[name] = data[pos:pos + size]
            pos += size
        self.end = pos

    def int(self, name):
        return struct.unpack("<i", self.values[name])[0]

    def ints(self, name):
        raw = self.values[name]
        return list(struct.unpack(f"<{len(raw) // 4}i", raw))

    def to_bytes(self):
        return b"".join(self.values[name] for name, _, _, _ in self.fields)


def cmd_info(path):
    with open(path, "rb") as fh:
        data = fh.read()
    h = SetHeader(data)
    print(f"{path} : version {h.version}, en-tête de {h.end} octets, fichier de {len(data)}")
    pos = 0
    for name, size, offset, text in h.fields:
        raw = h.values[name]
        if size == 4:
            shown = str(struct.unpack("<i", raw)[0])
        elif size in (0x10, 0x14, 0x28):
            shown = str(list(struct.unpack(f"<{size // 4}i", raw)))
        else:
            texts = [m.group().decode("latin-1")[:30] for m in re.finditer(rb"[ -~]{4,}", raw)][:2]
            shown = f"{sum(1 for b in raw if b)} octets non nuls" + (f", {texts}" if texts else "")
        where = f"objet +{offset:#07x}" if offset is not None and offset < 0x100000 else (
            f"{offset:#010x}" if offset else "")
        print(f"  {pos:#08x} {size:7d}  {name:22s} {where:16s} {shown}")
        pos += size


def find(paths, ext):
    found = []
    for p in paths:
        if os.path.isdir(p):
            for folder, _, names in os.walk(p):
                found += [os.path.join(folder, n) for n in names if n.lower().endswith(ext)]
        else:
            found.append(p)
    return sorted(found)


def cmd_check(paths):
    files = find(paths, ".set")
    failed = 0
    versions = set()
    for path in files:
        with open(path, "rb") as fh:
            data = fh.read()
        try:
            h = SetHeader(data)
            versions.add(h.version)
            problem = None
            if h.end != len(data):
                problem = f"{len(data) - h.end} octets après l'en-tête"
            elif h.to_bytes() != data:
                problem = "réécriture différente"
        except (FormatError, struct.error) as e:
            problem = f"{type(e).__name__} : {e}"
        if problem:
            failed += 1
            print(f"  ÉCHEC {path} : {problem}")
    print(f"{len(files)} aventures .set (versions {', '.join(map(str, sorted(versions)))}) : "
          f"{len(files) - failed} réécrites à l'octet près, {failed} en échec")
    sys.exit(1 if failed else 0)


def main(argv):
    if len(argv) == 3 and argv[1] == "info":
        cmd_info(argv[2])
    elif len(argv) >= 3 and argv[1] == "check":
        cmd_check(argv[2:])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
