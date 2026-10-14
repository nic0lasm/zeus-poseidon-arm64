# Reverse-engineering notes

Detailed notes are written in French and live next to the code:

| Document | Content |
|---|---|
| [`formats/sg3.md`](../formats/sg3.md) | sprite archives: `.sg3` index + `.555` pixels |
| [`formats/map.md`](../formats/map.md) | maps and saved games, image identifiers, terrain engine |
| [`analysis/notes/sources-et-binaires.md`](../analysis/notes/sources-et-binaires.md) | game editions, extraction chain, executables |
| [`analysis/notes/api-imports.md`](../analysis/notes/api-imports.md) | the 202 Windows functions the game imports, and which are implemented |
| [`analysis/notes/symbols.tsv`](../analysis/notes/symbols.tsv) | named addresses, each with its evidence |

## Method

- Every claim about a format or a function cites its evidence (offset, address, dump) and is
  marked **verified** or **hypothesis**.
- A format counts as understood when a tool in `tools/` reads 100 % of the files of that type
  without error.
- Each reader exists twice, in Python (reference) and C++ (port); a `--hash` option on both
  must give identical fingerprints.

## Executables

Two builds of the game have been studied. **Their addresses differ.**

| | GOG edition | CD edition, 2.1 |
|---|---|---|
| Built | 2002-01-04, Visual C++ 6 | 2001-10-31, Visual C++ 6 |
| Protection | none | SecuROM 4 |
| Use here | analysed and executed (in the emulator) | analysed only, never executed |

The game keeps its whole state in global variables (a 17 MB `.data` section), which is why a
saved game is essentially a list of memory blocks.

## Findings in the GOG executable

- **Window procedure.** Messages 2 to 16 are dispatched through a byte table at `0x005C59DC`
  and a jump table at `0x005C59C0`: `WM_MOVE` → `0x005C4C14`, `WM_SIZE` → `0x005C4BD1`.
- **Screen rectangle** at `0x00F2AB08` (four integers). In full-screen mode its only writer is
  the `WM_MOVE` handler, which stores `(0, 0, GetSystemMetrics(0), GetSystemMetrics(1))`.
- **Frame presentation**, function `0x0050C1B0`: in full-screen mode it calls
  `BltFast(primary, 0, 0, backbuffer, &rectangle, 0)`, and only once an elapsed-time counter
  reaches `0x32`. *Hypothesis:* the counter is in milliseconds, giving 20 frames per second.

### Screen size and layout

- `0x0050BCC0` sets the screen size from a mode number: 640×480, 800×600 or 1024×768, stored at
  `0x00F2A6B4` (width) and `0x00F2A6B0` (height). 68 functions read these variables. Menus
  always run in 800×600; the chosen mode applies to the city.
- Part of the layout is computed from the width; part is chosen by comparing the width with
  640, 800 or 1024, usually in a chain after one read.
- `0x0058E280` sets the position of the control panel: 478, 598 or 838 (width − 186, at
  `0x010DFF34`), and of the folded panel: width − 66 (`0x010DFF30`).
- `0x0051CC00` (panel unfolded) and `0x0051CBA0` (folded) pass the size of the map view to
  `0x00449E50` as a number of tile columns and rows: 10×39 at 800×600, 14×50 at 1024×768,
  17×50 folded. The view is 60 pixels per column and 15 per row, so its width is
  60 × columns − 2 and the screen sizes that fit exactly are 184 + 60 n by 33 + 15 n.
- `0x0059EBE0` draws the background of the top bar, an image 838 pixels wide, at (0, 0); in
  800×600 it clips it, in 640×480 it shifts it left.
- `0x0058E3E0` fills a 9-pixel strip at the bottom of the screen over a width given as the
  literal 1024.
- The game's frame buffer is at the address held in `0x00F9C100`, two bytes per pixel, one
  screen wide.

`src/win32/widescreen.cpp` uses all of the above to offer a wide mode.

### Text rendering and languages

- **Character table** at `0x0063BE2C`: one byte per character from `0x20` to `0xFF`, giving the
  glyph number within a font (0: no glyph). The GOG build maps Windows-1250; the French 2.1
  executable has the same table with Windows-1252 values (`à`: 84 against 120).
- **Fonts**: 17 groups of 134 glyphs in `Zeus_Fonts.sg3`, with the same layout and Western
  accents in both editions. The font group array is pointed to by `0x012AFBEC` (8 bytes per
  group, image identifier at +4).
- **Glyph placement.** For characters from `0x80` up, a glyph is raised by its height minus a
  line height, never less than zero. Two routines draw glyphs:
  - `0x00508B50` (single-line text) reads the line height from `0x00F13960`, set by its caller
    from the font: 23, 24, 9, 7 or 11 in the GOG build; the French build uses 29 for the two
    large fonts and 10, 11, 12 or 16 for the others (its function `0x005760A9`).
  - `0x004F59B0` (panel text) subtracts a constant 11 (at `0x004F5A33`); the French build
    subtracts 16, except for one font (its function `0x0055BF09`).
- **Polish exceptions.** Both GOG routines raise a fixed list of characters by 2 pixels (at
  `0x00508C7F` and `0x004F5ACF`). In Windows-1250 these are ą, ę, ć, ń, ł, ó, ś, ź, ż; in
  Windows-1252 the same codes are ê, æ, ñ, ó, œ… The French build has no such list.

`src/win32/language.cpp` applies the values of the other edition when a language pack is
loaded.

## Tools

`tools/api_imports.py` regenerates the import table of `analysis/notes/api-imports.md` from the
executable and the host code.

`tools/pe_triage.py FILE [--imports] [--strings REGEX]` gives a first look at any executable:
headers, sections, imports, known protection markers.

Ghidra is driven headless through `tools/ghidra/headless.sh PROJECT …`, which keeps Ghidra's
settings and cache inside the repository folder. Read-only queries:

```bash
tools/ghidra/headless.sh zeus/gog -process Zeus.exe -noanalysis -readOnly \
    -postScript Decompile.java OUT.c ADDRESS…
tools/ghidra/headless.sh zeus/gog -process Zeus.exe -noanalysis -readOnly \
    -postScript Xrefs.java OUT.tsv ADDRESS[:LENGTH]…
```

`zeus-run --watch-call ADDRESS` lists the call sites of a game function with their last
arguments, and `--dump-data FILE` writes the game's variables at exit; comparing two dumps (for
instance in two screen modes) quickly finds the variables that matter.

`ExportPcode.java OUT.txt ADDRESS…` writes the raw p-code of the functions containing the given
addresses, and `ExportFunctions.java OUT.tsv` the list of functions; together with
`zeus-run --profile` they tell which functions matter.

Addresses are hexadecimal without the `0x` prefix. Ghidra does not always create a reference
for accesses of the form `[base + index*4]`; searching the four little-endian bytes of an
address in `.text` finds them.

## Extracting the original CDs

The CD images, InstallShield cabinets and Wise patch installers of the 2000–2001 retail
editions can be unpacked with the Python tools, without running any installer:

```bash
python3 tools/cdimage.py extract CD.iso OUT_DIR
python3 tools/ishield.py extract DATA1.CAB OUT_DIR
python3 tools/wise.py extract PATCH.exe OUT_DIR
```

This is useful for studying the data of other language editions. The retail executables
themselves are copy-protected and are not run by this project.
