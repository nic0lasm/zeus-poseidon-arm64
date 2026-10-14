# `Model/*.txt` — simulation tables by difficulty

Reference reader: `tools/model.py` (`info FILE`, `check DIR…`, `dump FILE`; standard library only).
Offsets are positions in the game's memory (GOG `Zeus.exe` 2.1); file positions do not matter,
see "How the game reads them".

Status as of 2026-10-14: **63 of 63 files read in full** by two independent readers that must
agree (the game's own parsing algorithm re-implemented, and a line-based reader that keeps the
labels). The files come from the GOG edition, the English/French 2.1 installation, the Zeus 1.x
cabinet and the Poseidon cabinet.

These are plain text files (CRLF line endings, Latin-1), written by the developers as
spreadsheet exports. Four families, each in five difficulty levels except the tax table:

| Family | Files | Content | Records |
|---|---|---|---|
| building and house model | `Zeus_Model_<level>.txt` (VeryEasy, Easy, Normal, Hard, Impossible) | costs, desirability, employment, risks per building; evolution thresholds per house | 221 buildings + 13 houses (Zeus 1.x: 199 + 13) |
| figure model | `Figure_model_<level>.txt` (same levels) | combat and movement values per walker, soldier type and enemy unit | 78 figures + 66 enemies (Zeus 1.x: 72 + 36) |
| tax model | `Tax_Sentiment_Model.txt` (one file) | city sentiment per tax rate and difficulty | 7 rows |
| monster model | `monster_behavior_<level>.txt` (VeryEasy, Easy, Normal, Hard, VeryHard) | strength values per monster | 16 rows (Zeus 1.x: 12) |

The level order is the order of the game's difficulty setting. The game picks the file by indexing
a pointer table with the current difficulty (`DAT_00f2a988`): tables at `0x0065AF78` (Zeus_Model),
`0x0065AF8C` (Figure_model) and `0x00640A38` (monster_behavior), slot 0 = VeryEasy … slot 4 =
Impossible / VeryHard (verified: the slot of the Normal file is 2 in the three tables). The tax
table is a single file; its five difficulty columns are in the same order (verified by the loader,
status of the column names: hypothesis, taken from the file's own header).

## How the game reads them (verified)

The game does **not** parse lines. It reads the whole file into memory (`FUN_0050ACB0`, size by
`FUN_0050AEB0`), then:

1. looks for a section marker as a plain substring: the words "ALL BUILDINGS", "ALL HOUSES",
   "ALL FIGURES", "ALL ENEMIES" (the monster and tax files have no marker: scanning starts at the
   top of the file) — `FUN_00507C40`;
2. for each record, jumps just past the next `{` (`FUN_00507CA0`);
3. for each field, skips to the next digit or minus sign, at most 1 000 characters
   (`FUN_00508030`), reads an optionally signed decimal integer that stops at the first other byte
   (`FUN_00507FC0`), then jumps just past the next comma;
4. stops after the number of fields it needs. Anything else on the line is never read.

Consequences, all verified against the loaders and the reader:

* Only integers exist. A value written with a decimal point is read up to the point.
* Names, the leading index column and the trailing text are for humans. A building row of every
  Zeus_Model file carries an index that does not match its position (row 50 is numbered 10); the
  game ignores it.
* House rows have no closing brace; rows may carry columns after the values the game reads
  (derived values for the designers). The game never reads them.
* The record count is checked: the loader counts every `{` from the marker to the end of the file
  and shows a "model files out of sync" message box (answer *No* aborts loading) when the count is
  not what the executable expects. Buildings and houses are counted together (221 + 13 = 234 =
  `0xEA`), figures and enemies together (78 + 66 = 144 = `0x90`), tax 7, monsters 16 (`0x10`).
  Loops stop at 299 records at most. The Zeus 1.x files do not match the 2.1 counts: they belong to
  the 1.x executable.

## `Zeus_Model_<level>.txt`

Loader `FUN_005B0C20` (`0x005B0C20`).

**Buildings**: 221 records of 10 integers, stored from `0x00782F70`, 10 × u32 per building
(40 bytes), in file order (the file order is the building type id; verified by position).

| Field | Meaning | Varies with level | Status |
|---|---|---|---|
| 0 | cost of the structure (per tile for chained structures) | yes (96 rows) | hypothesis (file header) |
| 1 | initial desirability value | no | hypothesis (file header) |
| 2 | desirability step, in tiles | no | hypothesis (file header) |
| 3 | desirability step size (a positive value needs a negative step) | no | hypothesis (file header) |
| 4 | maximum desirability range (the header states a 6-tile limit) | no | hypothesis (file header) |
| 5 | number of people employed | no | hypothesis (file header) |
| 6 | fire risk increment | yes (46 rows) | hypothesis (file header) |
| 7 | damage risk increment | yes (31 rows) | hypothesis (file header) |
| 8 | resource used | no | hypothesis (file header) |
| 9 | risk reducer | no | hypothesis (file header) |

Unused slots (ids reserved by the game) and map-editor tile ids appear as all-zero rows.

**Houses**: 13 records of 21 integers, stored from `0x0078B24C`, 21 × u32 per house (84 bytes):
seven ordinary houses (the "hut" to the "townhouse"), then the elite housing rows (vacant,
devolved, four residence levels). Verified: 13 records, 21 fields each. Meaning of the 21 fields,
as numbered by the file's own header (hypothesis, not traced in code): devolve threshold and
evolve threshold of desirability, culture or science needed, water needed, stadium/museum
needed (the header skips number 4), soldier percentage, horse storage and horses needed, food types
needed, fleece, olive oil, wine, armor needed and armor storage, crime increment and base, one
unused, population capacity, tax-rate multiplier, one unused, disease increment. Eleven of the 21
fields vary with the level.

## `Figure_model_<level>.txt`

Loader `FUN_005B0E60` (`0x005B0E60`).

Two sections, both with records of 14 integers, 14 × u32 = 56 bytes per record in memory:

* **figures**: 78 records from `0x00786068` (marker "ALL FIGURES"); the first row has an empty
  index and is the "nobody" slot. Verified.
* **enemies**: 66 records from `0x007851F8` (marker "ALL ENEMIES"), grouped by six per invading
  army (infantry, missile, cavalry, three ship classes): the loader writes them in groups of six.
  Rows have one extra text column (the unit class) before `{`. Enemy rows are identical in the five
  levels (verified: no field differs).

| Field | Meaning | Varies with level | Status |
|---|---|---|---|
| 0 | fighting-unit type, a code from 0 to 10 (non-fighter, helpless, good guy, bad guy, criminal, predator, prey, good monster, bad monster, good god, bad god); for monsters and gods the scenario decides and the file value is ignored | no | hypothesis (file header) |
| 1 | hit points | yes (5 rows) | hypothesis (file header) |
| 2 | attack value | yes (4 rows) | hypothesis (file header) |
| 3 | armor value | yes (4 rows) | hypothesis (file header) |
| 4 | armor against missiles | yes (3 rows) | hypothesis (file header) |
| 5 | missile attack | yes (1 row) | hypothesis (file header) |
| 6 | missile range | yes (1 row) | hypothesis (file header) |
| 7 | missile rate of fire | yes (3 rows) | hypothesis (file header) |
| 8 | speed, a code from 0 to 18 (0 = still, 6 = one tile per step, each code adds one sixth up to 18 = three; codes 1 to 5 are 1/4, 1/3, 1/2, 2/3, 3/4) | no | hypothesis (file header) |
| 9 | frequency | no | hypothesis (file header) |
| 10 | morale level above which the unit turns defensive | no | hypothesis (file header) |
| 11 | percent chance of a special tactic | no | hypothesis (file header) |
| 12 | range of units used by the special tactic | no | hypothesis (file header) |
| 13 | unused | no | hypothesis (file header) |

## `Tax_Sentiment_Model.txt`

Loader `FUN_005B10F0` (`0x005B10F0`). Seven rows (tax rates none, very low, low, normal, high,
very high, outrageous), each `{rate, v1 … v5}` with six integers: the rate in percent goes to
`0x00641F50` (7 × u32), the five sentiment values to `0x00FE695C` (35 × u32, row after row, one
per difficulty level in the order of the five levels above). The header says that when tax coverage
is very low the first rows apply. Values go from positive (low taxes) to negative (high taxes) and
fall faster at harder levels. Counted: exactly 7 `{`. Verified (layout); meaning of the five
columns: hypothesis (file header).

## `monster_behavior_<level>.txt`

Loader `FUN_0052F460` (`0x0052F460`; table of file names at `0x00640A38`). One row per monster,
`name, behaviour word, {, 9 integers, }`. The game reads **eight** of the nine integers (one byte
each) into a 12-byte slot per monster starting at `0x00FE42F0`; the ninth is never read. In every
file of the five levels the behaviour word is the same for all rows (it is not read by the game).
The monster order is the monster id order used by the scenario files; the names are the in-game
monster names. Meaning of the eight bytes: hypothesis, none documented in the file; they scale
with the level in the files (strength values).

## Not covered here

* `Zeus eventmsg.txt`, `Zeus event message categories.txt`, `Zeus attitude advice.txt`: other
  text formats, in `Model/` too.
* `Sanctuary_*.txt`, `Poseidon_Pyramid_*.txt`, `Palace.txt`, `*_conan.txt`: placement lists.

## Tested files

All of `Model/Zeus_Model_*`, `Figure_model_*`, `Tax_Sentiment_Model.txt`, `monster_behavior_*` in
`install/`, `gog/app/`, `cab-zeus/` (Zeus 1.x) and `cab-poseidon/` (no tax file there): 63 files, 0
failures (`python3 tools/model.py check original`).
