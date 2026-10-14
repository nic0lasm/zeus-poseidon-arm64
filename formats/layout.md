# `Model/` building plans — sanctuaries, pyramids, palace

Reference reader: `tools/layout.py` (`info FILE`, `check DIR…`, `dump FILE`; standard library
only). Positions below are addresses in the GOG `Zeus.exe` 2.1.

Status as of 2026-10-14: **102 of 102 files read in full** (GOG, English/French 2.1
installation, Zeus 1.x and Poseidon cabinets). Plain text, CRLF, written by the developers.

| Files | Count per full set | Plan of | Fields per piece |
|---|---|---|---|
| `Sanctuary_<GOD>.txt` (fourteen gods), `Sanctuary_POSEIDON_GRAND.txt` and `Sanctuary_ZEUS_Pos.txt` | 16 | one sanctuary (the temple compound the player builds for a god) | 4 |
| `Poseidon_Pyramid_01.txt` … `_15.txt` | 15 | one pyramid-like monument of the Poseidon expansion | 5 |
| `Palace.txt` | 1 | the palace | 4 |
| `sanctuary_conan.txt`, `statue_conan.txt` | 2 | machine-written phase tables (see below) | n/a |

The Zeus 1.x cabinet holds 15 of these files and the Poseidon cabinet 19; the 2.1 sets hold 34.

## Which file for which structure (verified)

`FUN_0053E2E0` (`0x0053E2E0`) loads the whole set at start-up by calling `FUN_005400C0(id, path)`
for each file. `id` is the number of the plan: palace `0x75`; sanctuaries `0x54` … `0x62` (Zeus,
Poseidon, Demeter, Apollo, Artemis, Ares, Aphrodite, Hermes, Athena, Hephaistos, Dionysus, Hades,
Hera, Atlas, Poseidon_GRAND); pyramids `0x64` … `0x72` (files 01 to 15). `FUN_005400C0` keeps a
table of at most 31 plans (`0x1F`). `FUN_00510E80` reloads the Zeus plan alone: the Zeus
sanctuary file is `Sanctuary_ZEUS.txt`, or `Sanctuary_ZEUS_Pos.txt` when `FUN_004200F0(1)` is true
(hypothesis: that test is "Poseidon expansion active"; the two files have the same piece counts
and differ only in a few lines). The names of the files are strings at `0x0063D7F4` … `0x00641B70`.

## How the game reads a plan (verified)

`FUN_00540110` (`0x00540110`) reads the whole file and:

1. counts every `{` in the file, **comments included** (no `{` appears in a comment of any shipped
   file); a `{` is a piece;
2. for each `{`, calls `sscanf(text, "%d,%d,%d,%d")` (format at `0x00641B94`), or
   `"%d,%d,%d,%d,%d"` (`0x00641B84`) when the plan id is in `0x64`…`0x72`, i.e. for pyramids;
3. sends the piece to one of two lists: **type below 1000** to the main list, **type 1000 or
   more** to a second list. Each element is `0x48` bytes in memory.

The result of `sscanf` is not checked: variables live across iterations, so a field that cannot
be read **keeps the value of the previous piece**. This happens once in the shipped files (one
piece of the Artemis sanctuary has a missing comma and takes the elevation of the piece before
it). The reader reproduces this. The closing brace is not used: the Zeus_Pos sanctuary file has
4 fewer `}` than `{`.

Whitespace (spaces and tabs) around numbers is free. Text after `}` on the same line, `;`
comments and the legend at the top of the sanctuary files are never read.

## Piece

| Field | Meaning | Status |
|---|---|---|
| x | column of the piece, relative to the topmost tile of the structure (header: the temple is 4 × 4 or 4 × 8); may be negative | verified (stored at +0x00) |
| y | row, same origin; may be negative | verified (+0x04) |
| type | component id: parts of the sanctuary, altar, sculpture and statue of each god and direction, flag pole, steps, lifts, topper … The legend at the top of each sanctuary file lists the ids 0 to 109 and the pyramid files list 117 to 150 and 160 to 163; ids from 1000 are markers (see below) | verified (+0x08); names: hypothesis (file legend) |
| elevation | height level of the piece | verified (+0x10); unit: hypothesis |
| build order | pyramids only: step at which the piece is built (1, 2, 3 …) | verified (+0x14, zero for other plans) |

The game derives, for each piece, one value from `type` (`FUN_0053D960`, stored at +0x0C: hypothesis,
a sprite or class lookup) and twelve screen offsets from x and y (+0x18 to +0x44), one set per view
rotation: with 228 (`0xE4`) as the map row stride, the four values at +0x18 … +0x24 are
`x + 228·y`, `y + 228·x`, `−228·y − x`, `−228·x − y`, and the next eight scale `x − y` and
`x + y` by two constants read from the sprite tables (`DAT_007D3BB4 + 1`, `DAT_007D3BA4`).
Not saved or written back.

Types from 1000 go to the second list. Ids seen: 1000 to 1005 in sanctuaries (1000 is by far the
most frequent, 2 448 uses against 8 to 36 for each of the others, over the 2.1 set) and 1006 in
pyramids (one per file; the file legend names it the access pad). What they mean beyond that
(hypothesis: markers for tiles around or inside the plan, not buildings) is not traced.

Sizes in the 2.1 set: sanctuaries 11 to 197 pieces in the main list (Dionysus smallest, Zeus
largest) plus 28 to 148 in the second; pyramids 9 to 99 pieces plus exactly one second-list piece
each; the palace 24 pieces and none in the second list.

## `sanctuary_conan.txt` and `statue_conan.txt`

Both open with a comment saying the file is generated automatically. Structure, as read by the
reference reader (verified on all copies): any number of **phases**, each a line `{n}` followed
by exactly `n` rows `{x1, y1, x2, y2, look, distance, north, south, east, west}`. Comment lines
before the rows label the ten columns (start and finish tile, look direction, distance, four
rotation flags). In the shipped files `sanctuary_conan.txt` has four phases with no rows and
`statue_conan.txt` three phases with two rows in phase 1. The names are referenced at
`0x00535D60` and `0x00554E60`, code that Ghidra has not turned into functions: **how the game
uses them is not traced** (hypothesis: scripted camera or walker paths for cut scenes of the
sanctuary and statue).

## Not covered

The tables of `Zeus_Model` etc. are in [model.md](model.md). The event message files of `Model/`
are in a separate format.

## Tested files

All 102 files named above under `install/`, `gog/app/`, `cab-zeus/` and `cab-poseidon/`
(`python3 tools/layout.py check original`): 0 failures. The reader also fails a file with a `{`
that does not give a piece, or a data-looking line outside braces.
