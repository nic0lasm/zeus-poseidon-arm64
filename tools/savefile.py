#!/usr/bin/env python3
"""Lecture et réécriture à l'octet près des sauvegardes du jeu (`.sav`, voir formats/sav.md).

Usage :
    savefile.py info  FICHIER           table des blocs : rang, nom, adresse, forme, compression
    savefile.py check CHEMIN...         lit chaque .sav (fichiers ou dossiers), le réécrit et
                                        compare à l'original, octet par octet
    savefile.py dump  FICHIER DEST      écrit chaque bloc décompressé (DEST vide, dans le projet)
    savefile.py samples [--root DIR] [DEST]
                                        fait les sauvegardes d'essai avec zeus-run --deterministic
                                        (DEST : dossier neuf, défaut build/essais/sav-AAAAMMJJ-HHMMSS ;
                                        DIR : dossier qui contient build/zeus-run et original/,
                                        défaut le projet)

La décompression est celle de mapfile.py (explode). La compression est écrite ici : c'est
l'algorithme public de PKWARE DCL « implode », en mode binaire, avec un détail qui compte pour
retrouver les mêmes octets que le jeu : en fin de flux, la recherche de répétitions lit au-delà
des données, dans ce qui reste du bloc précédent puis dans la table qui suit le tampon. Le jeu
garde la même structure de travail pour toute une sauvegarde ; on fait de même.
"""
import multiprocessing
import os
import struct
import subprocess
import sys
import time

import mapfile

PROJECT_ROOT = mapfile.PROJECT_ROOT
TABLE_CAPACITY = 300
ENTRY_SIZE = 20
STORED_RAW = 0x80000000  # longueur d'un bloc compressible écrit sans compression

# --- compression PKWARE DCL « implode » --------------------------------------------------

MAX_REP = 0x204          # longueur maximale d'une répétition
WORK = 0x2204            # tampon : dictionnaire (0x1000) + 0x204 + bloc lu (0x1000)
END_OF_STREAM = 519      # longueur réservée qui termine le flux


def _emission_codes(table):
    """Codes à émettre (premier bit envoyé = bit 0) d'après une table canonique d'explode,
    dont les codes sont lus inversés."""
    count, symbol = table
    codes = {}
    code = index = 0
    for length in range(1, 14):
        for k in range(count[length]):
            inverted = ~(code + k) & ((1 << length) - 1)
            codes[symbol[index + k]] = (length, int(format(inverted, f"0{length}b")[::-1], 2))
        index += count[length]
        code = (code + count[length]) << 1
    return codes


def _length_codes():
    """(nombre de bits, valeur) de chaque longueur 2..519, bit « répétition » compris."""
    codes = {}
    length_symbols = _emission_codes(mapfile._LENCODE)
    for sym, (base, extra) in enumerate(zip(mapfile._BASE, mapfile._EXTRA)):
        nbits, code = length_symbols[sym]
        for e in range(1 << extra):
            codes[base + e] = (1 + nbits + extra, 1 | code << 1 | e << (1 + nbits))
    return codes


LENGTH_CODES = _length_codes()
_dist = _emission_codes(mapfile._DISTCODE)
DISTANCE_CODES = [_dist[i] for i in range(64)]


class Implode:
    """Compresseur. Une instance = une structure de travail, à garder pour toute une sauvegarde."""

    def __init__(self, dict_bits=6):
        self.dict_bits = dict_bits
        self.dsize = 64 << dict_bits
        # Le tampon de 0x2204 octets est suivi, dans la structure d'origine, de la table des
        # positions (u16) : on la garde aussi en octets à la suite pour les lectures qui débordent.
        self.work = bytearray(3 * WORK + 8)
        self.offs = [0] * (WORK + 8)       # positions triées par paire d'octets
        self.first = [0] * 0x900           # premier indice de chaque paire dans offs
        self.kmp = [0] * (MAX_REP + 2)
        self.distance = 0

    def _sort(self, begin, end):
        """Trie les positions [begin, end) par valeur de paire 4·b0 + 5·b1."""
        w, offs = self.work, self.offs
        counts = [0] * 0x900
        for p in range(begin, end):
            counts[w[p] * 4 + w[p + 1] * 5] += 1
        total = 0
        for i in range(0x900):
            total = (total + counts[i]) & 0xFFFF
            counts[i] = total
        for p in range(end - 1, begin - 1, -1):
            h = w[p] * 4 + w[p + 1] * 5
            i = counts[h] = (counts[h] - 1) & 0xFFFF
            offs[i] = p
            if i < WORK:
                w[WORK + 2 * i] = p & 0xFF
                w[WORK + 2 * i + 1] = p >> 8
        self.first = counts

    def _extend_kmp(self, inp, k, di, upto):
        """Prolonge la table des préfixes de la répétition en cours jusqu'à la longueur upto."""
        w, kmp = self.work, self.kmp
        while k < upto:
            if w[inp + k] != w[inp + di]:
                di = kmp[di]
                if di != 0xFFFF:
                    continue
            k += 1
            di = (di + 1) & 0xFFFF
            kmp[k] = di
        return k, di

    def _find_rep(self, inp):
        """Longueur de la meilleure répétition à la position inp (0 si aucune) ; la distance,
        moins 1, est laissée dans self.distance."""
        w, offs = self.work, self.offs
        h = w[inp] * 4 + w[inp + 1] * 5
        min_offs = (inp - self.dsize + 1) & 0xFFFF
        idx = self.first[h]
        if offs[idx] < min_offs:
            while offs[idx] < min_offs:
                idx += 1
            self.first[h] = idx
        prev = offs[idx]
        limit = inp - 1
        if prev >= limit:
            return 0
        rep_length = 1
        equal = 0
        # première passe : la plus ancienne occurrence la plus longue, arrêt au-delà de 10 octets
        while True:
            if w[inp] == w[prev] and w[inp + rep_length - 1] == w[prev + rep_length - 1]:
                equal = 2
                a, b = prev + 2, inp + 2
                while equal < MAX_REP and w[a] == w[b]:
                    equal += 1
                    a += 1
                    b += 1
                if equal >= rep_length:
                    self.distance = inp - prev - 1
                    rep_length = equal
                    if rep_length > 10:
                        break
            idx += 1
            prev = offs[idx]
            if prev >= limit:
                return rep_length if rep_length >= 2 else 0
        if equal == MAX_REP or offs[idx + 1] >= limit:
            return rep_length
        # seconde passe : une occurrence plus récente au moins aussi longue
        self.kmp[0], self.kmp[1] = 0xFFFF, 0
        k, di = self._extend_kmp(inp, 1, 0, rep_length)
        prev = offs[idx]
        prev_end = prev + rep_length
        rep2 = rep_length
        while True:
            rep2 = self.kmp[rep2]
            if rep2 == 0xFFFF:
                rep2 = 0
            while True:
                idx += 1
                prev = offs[idx]
                if prev >= limit:
                    return rep_length
                if prev + rep2 >= prev_end:
                    break
            pre_last = w[inp + rep_length - 2]
            if pre_last == w[prev + rep_length - 2]:
                if prev + rep2 != prev_end:
                    prev_end = prev
                    rep2 = 0
            else:
                while True:
                    idx += 1
                    prev = offs[idx]
                    if prev >= limit:
                        return rep_length
                    if w[prev + rep_length - 2] == pre_last and w[prev] == w[inp]:
                        break
                prev_end = prev + 2
                rep2 = 2
            while w[prev_end] == w[inp + rep2]:
                rep2 += 1
                if rep2 >= MAX_REP:
                    break
                prev_end += 1
            if rep2 >= rep_length:
                self.distance = inp - prev - 1
                rep_length = rep2
                if rep_length == MAX_REP:
                    return rep_length
                k, di = self._extend_kmp(inp, k, di, rep2)

    def compress(self, data):
        w = self.work
        dsize, dbits = self.dsize, self.dict_bits
        out = bytearray([0, dbits])  # mode binaire, taille du dictionnaire
        acc = nacc = 0

        def put(nbits, value):
            nonlocal acc, nacc
            acc |= value << nacc
            nacc += nbits
            while nacc >= 8:
                out.append(acc & 0xFF)
                acc >>= 8
                nacc -= 8

        def literal(p):
            put(9, w[p] << 1)

        pos = 0
        phase = 0
        ended = False
        inp = dsize + MAX_REP
        while not ended:
            chunk = data[pos:pos + 0x1000]
            pos += len(chunk)
            if len(chunk) < 0x1000:
                ended = True
                if not chunk and phase == 0:
                    break
            w[dsize + MAX_REP:dsize + MAX_REP + len(chunk)] = chunk
            # sans la fin des données, on garde 0x204 octets d'avance pour le tour suivant
            end = dsize + len(chunk) + (MAX_REP if ended else 0)
            if phase == 0:
                self._sort(inp, end + 1)
                phase = 1 if dsize == 0x1000 else 2
            elif phase == 1:
                self._sort(inp - dsize + MAX_REP, end + 1)
                phase = 2
            else:
                self._sort(inp - dsize, end + 1)
            while inp < end:
                rep = self._find_rep(inp)
                emitted = False
                while rep:
                    if rep == 2 and self.distance >= 0x100:
                        break
                    if ended and inp + rep > end:
                        rep = end - inp
                        if rep < 2 or (rep == 2 and self.distance >= 0x100):
                            break
                    elif rep < 8 and inp + 1 < end:
                        # une répétition plus longue un octet plus loin ?
                        saved = rep, self.distance
                        rep = self._find_rep(inp + 1)
                        if rep > saved[0] and (rep > saved[0] + 1 or saved[1] > 0x80):
                            literal(inp)
                            inp += 1
                            continue
                        rep, self.distance = saved
                    put(*LENGTH_CODES[rep])
                    low = 2 if rep == 2 else dbits
                    put(*DISTANCE_CODES[self.distance >> low])
                    put(low, self.distance & ((1 << low) - 1))
                    inp += rep
                    emitted = True
                    break
                if not emitted:
                    literal(inp)
                    inp += 1
            if not ended:
                inp -= 0x1000
                w[0:dsize + MAX_REP] = w[0x1000:0x1000 + dsize + MAX_REP]
        put(*LENGTH_CODES[END_OF_STREAM])
        if nacc:
            out.append(acc & 0xFF)
        return bytes(out)


# --- conteneur ----------------------------------------------------------------------------

class Block:
    __slots__ = ("rank", "compressed", "address", "element_size", "count", "converters",
                 "stored", "data")

    @property
    def size(self):
        return self.element_size * self.count


class SaveFile:
    """Sauvegarde lue en entier. `blocks` garde pour chaque bloc ses octets décompressés et la
    façon dont il était stocké (`stored` : 'raw', 'implode' ou 'raw80', ce dernier pour un bloc
    compressible écrit tel quel) ; `trailer` est ce qui suit le dernier bloc."""

    def __init__(self, data):
        self.version, count = struct.unpack_from("<II", data, 0)
        if not 3 <= count <= TABLE_CAPACITY:
            raise mapfile.FormatError(f"nombre de blocs invraisemblable : {count}")
        self.blocks = []
        for rank in range(count):
            b = Block()
            b.rank = rank
            (b.compressed, b.address, b.element_size, b.count,
             b.converters) = struct.unpack_from("<5I", data, 8 + ENTRY_SIZE * rank)
            self.blocks.append(b)
        head = [(b.compressed, b.size) for b in self.blocks[:3]]
        if head != [(0, 4), (0, 4), (0, TABLE_CAPACITY * ENTRY_SIZE)]:
            raise mapfile.FormatError(f"en-tête inattendu : {head}")
        pos = 0
        for b in self.blocks:
            if b.compressed == 0:
                b.stored, b.data = "raw", data[pos:pos + b.size]
                pos += b.size
            elif b.compressed == 1:
                length, = struct.unpack_from("<I", data, pos)
                pos += 4
                if length == STORED_RAW:
                    b.stored, b.data = "raw80", data[pos:pos + b.size]
                    pos += b.size
                elif length == 0:
                    b.stored, b.data = "empty", bytes(b.size)
                else:
                    b.stored = "implode"
                    b.data, used = mapfile.explode(data[pos:pos + length], b.size)
                    if used != length:
                        raise mapfile.FormatError(f"bloc {b.rank} : {length - used} octets après le flux")
                    pos += length
            else:
                raise mapfile.FormatError(f"bloc {b.rank} : indicateur {b.compressed}")
            if len(b.data) != b.size:
                raise mapfile.FormatError(f"bloc {b.rank} : {len(b.data)} octets sur {b.size}")
        self.trailer = data[pos:]
        # La table enregistrée (rang 2) doit décrire exactement les blocs lus.
        table = self.blocks[2].data
        if table[ENTRY_SIZE * count:] != bytes(len(table) - ENTRY_SIZE * count):
            raise mapfile.FormatError("entrées non nulles après le dernier bloc")

    @classmethod
    def load(cls, path):
        with open(path, "rb") as fh:
            return cls(fh.read())

    def to_bytes(self):
        out = bytearray()
        packer = Implode()  # une seule structure de travail pour tout le fichier, comme le jeu
        for b in self.blocks:
            if b.stored == "raw":
                out += b.data
            elif b.stored == "raw80":
                out += struct.pack("<I", STORED_RAW) + b.data
            elif b.stored == "empty":
                out += struct.pack("<I", 0)
            else:
                stream = packer.compress(b.data)
                out += struct.pack("<I", len(stream)) + stream
        return bytes(out + self.trailer)


# --- catalogue des blocs de la version 321 (adresses de l'exe GOG) ------------------------

# rang : (nom provisoire, statut) ; le détail et les preuves sont dans formats/sav.md.
BLOCK_NAMES = {
    0: ("version", "vérifié"), 1: ("block_count", "vérifié"), 2: ("block_table", "vérifié"),
    4: ("city", "vérifié en partie"), 5: ("year", "vérifié"), 6: ("month", "vérifié"),
    7: ("cities", "hypothèse"), 9: ("scenario", "hypothèse"), 13: ("table_9173f4", "hypothèse"),
    14: ("table_9173f4_count", "hypothèse"), 15: ("grid_edge", "hypothèse"),
    16: ("grid_building", "hypothèse"), 17: ("grid_terrain", "vérifié"),
    19: ("grid_figure", "hypothèse"), 20: ("grid_bitfield", "hypothèse"),
    22: ("grid_random", "hypothèse"), 28: ("figures", "vérifié"),
    32: ("padding", "vérifié"), 34: ("names", "hypothèse"), 35: ("buildings", "vérifié"),
    38: ("month_tick", "vérifié"), 39: ("tick", "vérifié"), 40: ("buildings_high", "vérifié"),
    41: ("padding", "vérifié"), 42: ("random_state_1", "vérifié"),
    43: ("random_state_2", "vérifié"), 63: ("figures_created", "vérifié"), 64: ("padding", "vérifié"),
    54: ("messages", "vérifié"), 55: ("messages_created", "vérifié"),
    56: ("messages_count", "vérifié"), 57: ("messages_last", "vérifié"), 68: ("buildings_last", "vérifié"), 83: ("buildings_created", "vérifié"), 101: ("table_9034f8", "hypothèse"),
    125: ("status_text", "hypothèse"),
}


def block_name(rank, address):
    return BLOCK_NAMES.get(rank, (f"block_{address:08x}", "inconnu"))[0]


# --- commandes ----------------------------------------------------------------------------

def cmd_info(path):
    s = SaveFile.load(path)
    print(f"{path} : version {s.version}, {len(s.blocks)} blocs, {len(s.trailer)} octets après")
    for b in s.blocks:
        print(f"{b.rank:4d}  {block_name(b.rank, b.address):22s}  0x{b.address:08X}  "
              f"{b.element_size:6d} × {b.count:<6d} {b.stored}")


def find_saves(paths):
    found = []
    for p in paths:
        if os.path.isdir(p):
            for root, _, files in os.walk(p):
                found += [os.path.join(root, f) for f in sorted(files) if f.lower().endswith(".sav")]
        else:
            found.append(p)
    return sorted(found)


def check_one(path):
    """(chemin, version, blocs compressés, None ou description de l'écart)."""
    with open(path, "rb") as fh:
        original = fh.read()
    try:
        s = SaveFile(original)
        rewritten = s.to_bytes()
    except mapfile.FormatError as e:
        return path, None, 0, f"FormatError : {e}"
    packed = sum(b.stored == "implode" for b in s.blocks)
    if rewritten == original:
        return path, s.version, packed, None
    n = next((i for i in range(min(len(original), len(rewritten))) if original[i] != rewritten[i]),
             min(len(original), len(rewritten)))
    return path, s.version, packed, f"premier écart à l'octet {n} ({len(rewritten)} octets sur {len(original)})"


def cmd_check(paths):
    files = find_saves(paths)
    if not files:
        sys.exit("aucun .sav trouvé")
    start = time.time()
    with multiprocessing.Pool() as pool:
        results = pool.map(check_one, files)
    failed = 0
    for path, version, packed, problem in results:
        if problem:
            failed += 1
            print(f"  ÉCHEC {path} : {problem}")
    versions = sorted({r[1] for r in results if r[1] is not None})
    print(f"{len(files)} sauvegardes (versions {', '.join(map(str, versions))}), "
          f"{sum(r[2] for r in results)} blocs compressés : {len(files) - failed} réécrites à "
          f"l'octet près, {failed} en échec ({time.time() - start:.0f} s)")
    sys.exit(1 if failed else 0)


def cmd_dump(path, dest):
    dest = mapfile.project_dir(dest)  # crée le dossier, refuse s'il n'est pas vide
    s = SaveFile.load(path)
    for b in s.blocks:
        name = f"{b.rank:03d}_{block_name(b.rank, b.address)}_{b.address:08x}.bin"
        with open(os.path.join(dest, name), "wb") as fh:
            fh.write(b.data)
    print(f"{len(s.blocks)} blocs écrits dans {dest}")


# Parcours d'essai, en 800 × 600. Ouverture du profil N puis du premier didacticiel de
# Poséidon (même début que tests/scenarios) ; sauvegarde par File → Save adventure, nom « AB ».
_PROFILE_N = "5:key 27; 12:click 220,101; 17:char 78; 20:click 556,320; 27:click 644,523"


def _save_at(t):
    """Capture de l'écran (avant.bmp) puis sauvegarde « AB » à l'instant t."""
    return (f"{t - 1}:shot avant.bmp; {t}:click 20,10; {t + 2}:click 80,100; {t + 5}:char 65; "
            f"{t + 6}:char 66; {t + 8}:click 490,406")


SAMPLES = {
    # une ville sans rien : première aventure de Poséidon (profil P), avant toute construction
    "vide": (85, "5:key 27; 12:click 580,101; 22:char 80; 25:click 556,320; 36:click 590,260; "
                 "48:click 315,442; 62:click 656,491; " + _save_at(70)),
    # quelques bâtiments et marcheurs : premier didacticiel à l'ouverture
    "didacticiel": (70, _PROFILE_N + "; 33:click 212,120; 45:rclick 300,300; " + _save_at(52)),
    # le même, quatre minutes de jeu plus tard
    "didacticiel-tard": (300, _PROFILE_N + "; 33:click 212,120; 45:rclick 300,300; " + _save_at(280)),
    # constructions faites au cours de la partie : route et terrains à bâtir
    "constructions": (180, "5:key 27; 12:click 580,101; 22:char 80; 25:click 556,320; "
                           "36:click 590,260; 48:click 315,442; 62:click 656,491; 80:click 666,451; "
                           "81:move 400,400; 82:down 330,315; 84:move 360,330; 85:move 390,345; "
                           "86:move 420,360; 87:move 450,375; 88:move 480,390; 89:move 510,405; "
                           "90:move 540,420; 91:move 570,435; 92:up 570,435; 94:click 622,42; "
                           "96:click 677,345; 97:move 400,300; 99:click 388,285; 100:click 301,330; "
                           "101:click 448,315; 102:click 361,360; 103:click 508,345; "
                           "104:click 421,390; 105:click 568,375; 106:click 481,420; "
                           "107:rclick 300,200; " + _save_at(160)),
    # grande ville : dernier didacticiel (See a working city)
    "grande-ville": (80, _PROFILE_N + "; 33:click 588,476; 45:rclick 300,300; 50:click 568,574; "
                         "55:click 568,574; " + _save_at(60)),
}


def _run_sample(args):
    name, seconds, script, dest, root = args
    write_dir = os.path.join(dest, name)
    os.makedirs(write_dir)
    result = subprocess.run(
        [os.path.join(root, "build", "zeus-run"), "--run",
         os.path.join(root, "original", "gog", "app", "Zeus.exe"), "--headless",
         "--deterministic", "--mute", "--write-dir", write_dir, "--seconds", str(seconds),
         # chemin relatif : le script de zeus-run coupe ses actions aux espaces
         "--script", script.replace("shot avant.bmp", "shot " + os.path.join(
             os.path.relpath(write_dir, root), "avant.bmp"))],
        capture_output=True, text=True, cwd=root)
    saves = find_saves([write_dir])
    return name, result.returncode, saves


def cmd_samples(args):
    root = PROJECT_ROOT
    if args[:1] == ["--root"]:
        root, args = os.path.abspath(args[1]), args[2:]
    dest = os.path.abspath(args[0] if args else os.path.join(
        root, "build", "essais", time.strftime("sav-%Y%m%d-%H%M%S")))
    if os.path.commonpath([dest, root]) != root:
        sys.exit(f"{dest} : hors du projet")
    os.makedirs(dest, exist_ok=False)  # dossier neuf : le jeu part de zéro
    jobs = [(n, s, sc, dest, root) for n, (s, sc) in SAMPLES.items()]
    with multiprocessing.Pool(len(jobs)) as pool:
        for name, code, saves in pool.imap_unordered(_run_sample, jobs):
            missing = "" if any(os.path.basename(p) == "AB.sav" for p in saves) else "  AB.sav MANQUANTE"
            print(f"{name:18s} code {code}  "
                  + ", ".join(os.path.relpath(p, dest) for p in saves) + missing)
    print(dest)


def main(argv):
    if len(argv) >= 3 and argv[1] == "info":
        cmd_info(argv[2])
    elif len(argv) >= 3 and argv[1] == "check":
        cmd_check(argv[2:])
    elif len(argv) == 4 and argv[1] == "dump":
        cmd_dump(argv[2], argv[3])
    elif len(argv) >= 2 and argv[1] == "samples":
        cmd_samples(argv[2:])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
