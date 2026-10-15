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

- `0x00510ED0` is the save routine and `0x0050E2C0` writes the file: one pass each while
  saving, none otherwise (`--count`);
- `0x0050CB80` is called once per block: 139 passes for that one save;
- the file's own block table (block 2, kept at `0x00F9C1D0` while saving) gives, for each block,
  the address of the variable in this executable, the size of an element and the number of
  elements. The file format itself is described in `formats/sav.md`.

Blocks 0 to 2 describe the file (version, number of blocks, the table) and are left out. The 136
others are the state zones, in save order: 4.5 MB in all. Four zones are added that the save does
not hold. The first is the random generator's working area of the tick, `0x00F13624` to `0x00F137E8`.
`0x00513A30`, the tick, stores the last draw in a ring of 100 values (index at `0x00F13624`,
values from `0x00F13628`), steps the two generator registers (`0x00F137E0`, `0x00F137E4`, saved as
blocks 43 and 42) and derives six values from them (`0x00F137C4` to `0x00F137D8`). The three others are
variables the game recomputes when it loads a game (`formats/sav.md`): the tile index offset,
bounds of the playable area and view orientation (`0x00DA2F78` to `0x00DA2F8C`), the last used
slot of block 81 (`0x0091EBF0`), and the list of free figure numbers (`0x00DBB84C`, rebuilt by
`0x004D9F40`; its size, up to block 28, is a hypothesis).

### What the zones are

| Zones | Addresses | Evidence |
|---|---|---|
| map grids, 228 × 228 cells (51,984 entries) | 21 blocks, among them `0x00D404B8` (drawing bits), `0x00D26E98` (building), `0x00CCE128` (terrain), `0x00D0D878` (first figure), `0x00D00D68` (slope), `0x008E01FA` (height) | element count of a map; the named ones are read by the city drawing loop (`src/win32/game_gog.h`) |
| figures (walkers, animals, soldiers…) | `0x00DBD788`, 2,000 records of 388 bytes | the drawing loop reads records of `0x184` bytes from `0x00DBD790` |
| buildings | `0x0092E658`, 4,000 records of 280 bytes | the drawing loop reads records of `0x118` bytes from `0x0092E668` |
| random generator | `0x00F137E0`, `0x00F137E4` (saved), `0x00F13624`–`0x00F137E8` (tick) | `0x00513A30` |
| everything else | 107 smaller blocks | written by the save routine; meaning not established here |

In the tutorial scenario, 33 of the first 137 zones change, and no two ticks have the same state.
Some zones hold heap addresses (block 101 keeps a pointer at +4 of each element, which the save
routine frees first, see `formats/sav.md`). They are fingerprinted as they are: the host's heap
hands out the same addresses in the same order in every deterministic run.

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
replaced natively). `START` must be reached once per pass: for a loop, the instruction
before it, not its head (which a pass reaches again at the same stack depth, reported as a
failure). `--verify-every N` checks one pass in N. On each checked pass:

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
stack below the final stack pointer (locals of finished calls) and, for the functions listed in
`equivalence.cpp`, the argument slots of the call. The original code reuses them as variables, as
Visual C++ does; the caller pops them without reading them back. Not compared: `ECX` and `EDX`
after a function (never read by a caller with Visual C++'s calling conventions), `EAX` after a
function that returns nothing (signatures from Ghidra) or whose callers never read the value
(`0x00457900`: both callers overwrite `EAX` first), all listed in `equivalence.cpp`, the flags,
the x87 and SSE registers (none of today's replacements returns a value there). For a piece of code, the registers left out are those its replacement proves
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

If the checked code reaches its start again at the same stack depth without having reached its
end, or if the run ends in the middle of a checked pass, that is reported as a failure too.

A call during which the original calls a host function (a Windows import, the clock, the heap)
cannot be replayed identically: the host's own state moved. Such a call keeps the original's
result and is counted as left to the original, not compared.

**Limits.** Other interceptions on the checked address (`--count`, `--state-log` on the same
address) see each of the two executions. Each checked pass copies all writable memory three times (about 25 ms for a city),
hence the sampling for functions called millions of times. The inputs of a failing pass are
reported but not kept as a separate regression case.

## Results for today's replacements

Measured on 2026-10-15 over the nine scenarios of `tests/scenarios` (43,159 frames, 13,073
simulation ticks).

**State, tick by tick.** With the references recorded under `--no-native`, the host with all its
native replacements gives the same frames and the same fingerprint of all 140 zones at every one of
the 13,073 ticks, in all nine scenarios.

**Call by call** (`check_scenarios.py --verify TARGET --every N`, all nine scenarios):

| Target | What | Checked | Passes | Compared | Left to the original | Scenarios with calls | Result |
|---|---|---|---|---|---|---|---|
| `0x00413DC0` | opaque rectangle copy | 1 in 1 | 2,568 | 2,568 | 0 | 1 | identical |
| `0x00413E30` | run-length image, blended | 1 in 2 | 50,024 | 25,015 | 0 | 9 | identical |
| `0x00416010` | rectangle copy, magenta transparent | 1 in 1 | 45,591 | 45,591 | 0 | 9 | identical |
| `0x004161B0` | run-length image, optionally a shadow | 1 in 100 | 1,997,500 | 19,979 | 0 | 9 | identical |
| `0x00457900` | ground of one city tile | 1 in 100 | 6,043,020 | 60,435 | 0 | 9 | identical |
| `0x00458170` | the whole city drawing loop, with every native piece it reaches (ground loop jump, ground tile, sprite dispatcher, diamond tile) | 1 in 1 | 20,391 | 20,391 | 0 | 9 | identical |
| `0x005059F8:0x00505A33` | key lookup loop of `Zeus eventmsg.txt` | 1 in 50 | 75,294 | 1,512 | 0 | 9 | identical |
| `0x0050B410` | frame clear | 1 in 1 | 13,131 | 13,131 | 0 | 9 | identical |
| `0x005ACF50` | sprite dispatcher | 1 in 3,000 | 132,799,553 | 44,264 | 7 | 9 | identical |
| `0x005CC640` | `_strcmpi` | 1 in 300 | 10,626,752 | 35,426 | 0 | 9 | identical |

The seven calls of the sprite dispatcher left to the original are cases it does not handle
natively, whose original code calls the host. The first runs found three kinds of difference,
all outside the result and now excluded with their evidence: the argument slots reused by
`0x00413E30`, `0x004161B0` and `0x00457900`, and the `EAX` of `0x00457900`, which its callers
never read. The rows of `0x00413DC0`, `0x00416010`, `0x0050B410`, `0x005CC640`, `0x00458170`
and the key lookup loop come from an earlier run of the same bench, which also compared argument
slots and covered 137 zones.

Every native replacement of `src/win32/native.cpp` and `native_draw.cpp` is covered: the ground
loop jump (`0x0045829D`) and the diamond tile (`0x00435070`) run inside `0x00458170`.
