# Equivalence harness

The plan is to replace the original game code piece by piece with hand-written native code, the
emulated original serving as the judge. This page describes the two instruments that prove a
replacement exact, and what they say about the replacements that exist today. Code:
`src/win32/equivalence.cpp`; scenario bench: `tools/check_scenarios.py`.

- **State fingerprint, tick by tick.** At every simulation tick, a fingerprint of every memory
  zone that makes up the game state. The pure original (`--no-native`) and the host with its
  native replacements must write the same fingerprints, tick for tick, in every scenario.
- **Targeted check of one function.** On sampled calls of a game function, the call runs twice
  from the same state, once with the native replacements off and once with them on; registers
  and memory are compared, and the run stops at the first difference with a report.

The frame logs of the scenarios remain: they prove that every displayed frame is identical. The
two instruments above add what frames cannot show (state that is not on screen, or not yet) and
say *where* a difference comes from.

## Game state

### Where it comes from

The zones are the variables the game writes into a saved game. A game saved with the GOG
`Zeus.exe` under `zeus-run` (*File → Save adventure* in the last tutorial) was traced:

- `0x00510ED0` is the save routine: one pass while saving, none otherwise (`--count`);
- `0x0050CB80` writes one block: 139 passes for that one save, one per block of the file;
- the file's own block table (block 2, kept at `0x00F9C1D0` while saving) gives, for each block,
  the address of the variable in this executable, the size of an element and the number of
  elements. The file format itself is described in `formats/map.md`.

Blocks 0 to 2 describe the file (version, number of blocks, the table) and are left out. The 136
others are the state zones, in save order: 4.5 MB in all. One zone is added that the save does
not hold: the random generator's working area of the tick, `0x00F13624` to `0x00F137E8`.
`0x00513A30`, the tick, stores the last draw in a ring of 100 values (index at `0x00F13624`,
values from `0x00F13628`), steps the two generator registers (`0x00F137E0`, `0x00F137E4`, saved as
blocks 43 and 42) and derives six values from them (`0x00F137C4` to `0x00F137D8`).

### What the zones are

| Zones | Addresses | Evidence |
|---|---|---|
| map grids, 228 × 228 cells (51,984 entries) | 21 blocks, among them `0x00D404B8` (drawing bits), `0x00D26E98` (building), `0x00CCE128` (terrain), `0x00D0D878` (first figure), `0x00D00D68` (slope), `0x008E01FA` (height) | element count of a map; the named ones are read by the city drawing loop (`src/win32/game_gog.h`) |
| figures (walkers, animals, soldiers…) | `0x00DBD788`, 2,000 records of 388 bytes | the drawing loop reads records of `0x184` bytes from `0x00DBD790` |
| buildings | `0x0092E658`, 4,000 records of 280 bytes | the drawing loop reads records of `0x118` bytes from `0x0092E668` |
| random generator | `0x00F137E0`, `0x00F137E4` (saved), `0x00F13624`–`0x00F137E8` (tick) | `0x00513A30` |
| everything else | 107 smaller blocks | written by the save routine; meaning not established here |

During the tutorial scenario 33 of the 137 zones change; no two ticks have the same state.

### `--state-log`

`zeus-run --state-log FILE` writes one header line per zone (`# zone BLOCK ADDRESS SIZE NAME`),
then one line per simulation tick: the tick number and a 64-bit fingerprint of each zone, taken
at the entry of `0x00513A30` (the state left by the previous tick and by everything since: the
player's orders, a loaded game). The last line, `fin`, is the state at the end of the run.

`tools/check_scenarios.py` writes it for every scenario. `record` now records the references
with `--no-native`: frames and state of the pure original. A normal run (native replacements on)
must give the same frame log and the same state log; for the state, the first differing tick and
the zones involved are printed.

## Targeted check

```bash
./build/zeus-run --run original/gog/app/Zeus.exe --headless --deterministic \
    --script "…" --verify 0x0050B410
./build/zeus-run … --verify 0x005059F8:0x00505A33 --verify-every 100
python3 tools/check_scenarios.py --verify 0x005ACF50 --every 5000
```

`--verify ADDRESS` checks a function from its entry to its return; `--verify START:END` checks a
piece of code from `START` until execution reaches `END` with the same stack pointer (a loop
replaced natively). `--verify-every N` checks one pass in N. On each checked pass:

1. at the entry, the processor state and all writable memory are copied (the image's sections,
   the part of the heap already handed out, the stack, the thread block);
2. the native replacements are switched off (`Cpu::set_native`: original bytes put back, fast
   paths and host jumps let the original run) and the call runs; its registers and memory are
   copied, then the entry state is put back;
3. the replacements are switched on again and the same call runs again;
4. the registers and all memory are compared. The game then goes on from that state.

The replacements are switched off for the whole call, including the functions it calls: what is
checked is "the original code alone" against "the host as it ships".

Compared: `EAX`, `EBX`, `ESP`, `EBP`, `ESI`, `EDI` and every byte of writable memory, except the
stack below the final stack pointer (locals of finished calls). Not compared: `ECX` and `EDX`
after a function (never read by a caller with Visual C++'s calling conventions), `EAX` after a
function that returns nothing (signatures from Ghidra, listed in `equivalence.cpp`), the flags,
the x87 registers. For a piece of code, the registers left out are those its replacement proves
dead (`EAX` and `ECX` for the key lookup loop).

On the first difference the run stops (exit code 1) with the pass number, the caller and the
arguments, the registers that differ, and the first differing byte with what it belongs to:

```
vérification 0x0050b410 : 2 passages, 1 comparés (1 sur 1), DIFFÉRENCE
passage 2, appelé depuis 0x005900bd, arguments … :
mémoire : 2 octets diffèrent, le premier en 0x1027b16e (tampon d'affichage, x 799, y 599)
  origine : 00 00 00 00 00 10 …
  natif   : a1 18 00 00 00 10 …
```

(a deliberate fault: the native clear stopping one pixel short.)

**Limits.** A call during which the original calls a host function (a Windows import, the
clock, sound) cannot be replayed identically: the host's own state moved. The check refuses it
and stops. Each checked pass copies all writable memory three times (about 25 ms for a city),
hence the sampling for functions called millions of times. The inputs of a failing pass are
reported but not kept as a separate regression case.

## Results for today's replacements

RESULTS
