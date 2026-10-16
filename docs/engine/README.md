# The engine, piece by piece

The plan: replace the original code of *Zeus* and *Poseidon* one piece at a time with an engine
written by hand and documented, with the original game as the judge (see the
[roadmap](../roadmap.md)). This page counts how far that has gone. The numbers come from two
files and two tools, so anyone can reproduce them:

| What | Where |
|---|---|
| Every function of the GOG executable: address, size, our name, module, status, evidence | [`analysis/symbols.tsv`](../../analysis/symbols.tsv) |
| Names written by hand, each with its evidence | [`analysis/noms.tsv`](../../analysis/noms.tsv) |
| Counters | `python3 tools/coverage.py [--profile PROFILE.tsv]` |
| Profile of the emulated game | `zeus-run --no-native --profile PROFILE.tsv` |
| Functions the simulation tick reaches, their branches and callers | [`analysis/simulation.tsv`](../../analysis/simulation.tsv), from `zeus-run --call-graph` and `tools/simulation_map.py` ([simulation](simulation.md)) |

Subsystem pages: [rendering](rendering.md), [interface and texts](interface.md),
[terrain](terrain.md), [simulation](simulation.md) (the map of the tick).

Only addresses, sizes and our own names are published: never code, bytes or decompiled text of
the game.

## Counters

- **Native functions: 8 of 4,872** (0.2 %; figures of 2026-10-15, after #86). A function is *native* when the host replaces the
  whole of it. Four more are replaced only in their common cases (a fast path or a rewritten
  loop, the original code still running for the rest): they stay *named* and say so in their
  evidence column.
- **Functions understood: 294 of 4,872** (6.0 %): named, documented or native (figures of
  2026-10-16, after the map of the simulation).
- **Computation done natively: 48.7 %, plus 14.7 % partly native.** Scenario `working-city` (the last tutorial, a large city)
  for its full 300 s, deterministic clock, run with `--no-native` so that the whole game is
  emulated and profiled. The profile times every block of x86 code the game executes (corrected
  for the cost of the measure, as in `tools/profile_report.py`). **48.7 %** of that time is spent
  in the 8 native functions, plus **14.7 %** in functions that are only partly native; the rest is
  original code that is still emulated. The share of executed code bytes gives the same answer
  (56.1 % + 13.0 %). It is a share of the *game's own code*: the time the host spends in Win32
  calls, blits and sound (blocks outside the game's `.text`, 8.7 % of the time measured) is
  excluded. Caveats: the profiler itself slows the run down and its cost is estimated, a byte
  count is not an instruction count (a repeated string instruction counts once), and one
  scenario is not the whole game.

Reproduce:

```bash
./build/zeus-run --run original/gog/app/Zeus.exe --headless --deterministic --mute --no-native \
    --write-dir build/essais/profil --seconds 300 --profile build/essais/profil.tsv \
    --script "$(grep -v '^#' tests/scenarios/working-city.txt | tr '\n' ';' | sed 's/;$//')"
python3 tools/coverage.py --profile build/essais/profil.tsv
```

## Statuses

| Status | Meaning |
|---|---|
| unknown (`inconnu`) | Ghidra found the function; we have not looked at what it does |
| named (`nommé`) | we gave it a name, with the evidence in `analysis/noms.tsv` |
| documented (`documenté`) | named, and its behaviour is written up in `docs/` or `formats/` |
| native (`natif`) | replaced by host code that is checked against the original |

## By module

| Module | Fonctions | Inconnu | Nommé | Documenté | Natif |
|---|---:|---:|---:|---:|---:|
| rendu | 147 | 102 | 24 | 10 | 11 |
| interface | 231 | 157 | 1 | 73 | 0 |
| texte | 32 | 10 | 1 | 21 | 0 |
| terrain | 92 | 42 | 15 | 35 | 0 |
| marcheurs | 242 | 226 | 12 | 4 | 0 |
| bâtiments | 178 | 139 | 15 | 24 | 0 |
| économie | 33 | 17 | 5 | 11 | 0 |
| dieux et héros | 27 | 27 | 0 | 0 | 0 |
| armée | 18 | 14 | 2 | 2 | 0 |
| scénario | 257 | 237 | 5 | 15 | 0 |
| sauvegarde | 22 | 22 | 0 | 0 | 0 |
| son | 122 | 117 | 0 | 5 | 0 |
| bibliothèque C | 278 | 276 | 1 | 0 | 1 |
| inconnu | 3193 | 3192 | 1 | 0 | 0 |
| **Total** | **4872** | **4578** | **82** | **200** | **12** |

The *module* column says where a function sits in the game, not who wrote it. A function is
placed from the strongest evidence available; the evidence is in the `preuve` column of
`symbols.tsv`:

1. a **string** the function cites (a file name or message: `.wav` → sound, `.sav` → saved
   games, DirectDraw messages → rendering…);
2. an **import** it calls directly (Miles Sound System → sound, Bink and DirectDraw → rendering);
3. a **name** given by hand (`analysis/noms.tsv`);
4. a **hypothesis**, always spelled as such: the C library zone at the end of the executable;
   the branch of the simulation tick a function serves ([simulation §7](simulation.md#7-placing-functions-in-modules));
   or a vote of the callers and callees already placed (a helper called only from sound code is
   probably sound code).

Most functions stay *unknown* on purpose: no evidence, no module. As a check, the placement was
run with the hand-given names hidden: of the 45 named functions it could reach, 6 landed in the
right module, 1 in a wrong one (the main loop, placed by a start-up message) and 38 were left
unknown. Adjacency in the executable was tried as a hint and dropped: more than half of its
answers were wrong.

The code map is produced by a read-only Ghidra script (`tools/ghidra/ExportCodeMap.java`, run as
in [reverse-engineering.md](../reverse-engineering.md)) and `tools/build_symbols.py`. The Ghidra
project of the GOG executable holds 4,842 functions with a body; 30 more are named in our notes
at addresses Ghidra did not mark as function starts: **4,872 in all**. The C library is
*included* (281 functions, 6 %), and so is the part of the executable that no module claims.

### Why not 8,397?

8,397 is the number of functions in `.text` of the **2.1 edition** of `Zeus.exe`
(`analysis/notes/sources-et-binaires.md`); the 2.0 edition has about 4,900 and the GOG edition
4,858 before our additions. The GOG executable is the only one the game runs from, and its
addresses are the only ones in this map, so every counter is out of 4,872. If the other editions
are mapped later, they get their own totals.
