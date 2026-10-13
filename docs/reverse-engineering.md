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

## Tools

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
