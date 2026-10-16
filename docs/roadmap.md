# Roadmap

**The aim: keep *Zeus* and *Poseidon* playable for as long as people want to play them — the
original game, unmodified in behaviour, on the machines people actually own, and proven identical
to the original.**

The game is playable today (version 1.2, Apple Silicon Macs). What follows replaces the original
code **piece by piece with an engine written by hand and documented**. The emulated original stays
the reference and the judge: every rewritten part must prove, turn by turn, that it leaves the
game in exactly the same state. No code is translated mechanically. The precedent is
[Julius](https://github.com/bvschaik/julius), the same work done for *Caesar III*.

No dates are promised: each step is done when it is proven, not before.

```mermaid
flowchart LR
    P["<b>1.x Playable</b><br/>today: 1.2<br/><i>released</i>"] --> A["<b>A Instruments</b><br/>equivalence harness,<br/>code map, counters"]
    A --> D["<b>B Data</b><br/>savegame, maps, images,<br/>texts, tables"]
    D --> C["<b>C Native engine</b><br/>subsystem by subsystem"]
    C --> N["<b>2.0 Native engine</b><br/>no emulator to play<br/><i>on hold</i>"]
    N -.-> M["<b>Modding</b><br/>separate branch"]
    N -.-> X["Exploration<br/>City Building Series"]
```

| Step | Theme | State | Follow it |
|---|---|---|---|
| **1.x — Playable** | The game as it is: lighter on the machine, every language edition, a simpler first launch; Intel Macs and Linux with the same host | released (1.2); next: saves without a pause, Intel Macs, Linux | [milestone](https://github.com/nic0lasm/zeus-poseidon-arm64/milestone/2) · [#22](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/22) · [#24](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/24) · [#55](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/55) · [#40](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/40) · [#41](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/41) |
| **Phase A — Instruments** | Equivalence harness on the full game state; code map; two public counters (functions understood out of 4,872, share of the computation done natively) | done; counters kept current | [milestone](https://github.com/nic0lasm/zeus-poseidon-arm64/milestone/7) · [#26](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/26) · [#69](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/69) |
| **Phase B — Data** | Savegame `.sav` described in full, then maps and adventures, images, texts, tables; each format read then written back byte for byte | done for what the engine reads | [milestone](https://github.com/nic0lasm/zeus-poseidon-arm64/milestone/8) · [#70](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/70) · [#30](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/30) |
| **Phase C — Native engine** | Rendering, interface and texts, terrain, movement and walkers, housing and buildings, economy, gods, heroes and monsters, army, events and adventures, savegame | rendering largely native, simulation and interface in part; beyond that, only lots with a measured gain in real play | [milestone](https://github.com/nic0lasm/zeus-poseidon-arm64/milestone/9) · [#71](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/71) |
| **2.0 — Native engine** | No emulator to play | on hold (review of 16 October 2026) | [milestone](https://github.com/nic0lasm/zeus-poseidon-arm64/milestone/3) · [#25](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/25) · [#38](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/38) · [#39](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/39) · [#40](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/40) · [#41](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/41) |
| **Modding (branch)** | Changes to the game, on a `modding` branch | later | [milestone](https://github.com/nic0lasm/zeus-poseidon-arm64/milestone/10) · [#72](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/72) |
| exploration | The City Building Series: *Emperor*, *Pharaoh*, *Caesar III* | on hold | [milestone](https://github.com/nic0lasm/zeus-poseidon-arm64/milestone/5) · [#28](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/28) · [#29](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/29) |

## After 1.2 (review of 16 October 2026)

Version 1.2 is out: 24 functions native, 53.2 % of the game's computation done natively plus
17.1 % partly, a small city at about 23 % of a core on a Mac. The seven steps of the review
below are done or stopped. This review measures where the time goes now and orders what is left
by what a player would notice.

### Where the time goes now

`main` after #113 (`0c4ed9b`): the script of `working-city` played for 150 s of real time,
native code on, headless, on one core of a cloud machine (x86-64 Linux, Unicorn 2.1.4); 77.7 s
of it is active play in the large city, 43.7 ticks a second. The process used 32.4 s of CPU.
`--time-span` from each call site to its return:

| Part of the loop turn | Call site | Per pass | Over the run | Share of the process's CPU |
|---|---|---|---:|---:|
| simulation tick | `0x005135C3` | 3.0–3.3 ms (3,407 ticks) | 10.1 s | 31 % |
| screen drawing | `0x005137D1` | 1.9 ms (7,117 turns) | 13.4 s | 41 % |
| — of which the city map | `0x00514090` | 2.7 ms (3,457 frames) | 9.4 s | 29 % |
| — of which the side panel | `0x00514095` | 0.36 ms | 1.2 s | 4 % |
| input pass | `0x005137D6` | 0.38 ms | 2.7 s | 8 % |
| presentation (almost all in the host's copies, [interface §10](engine/interface.md#10-cost-in-real-time)) | `0x00513825` | 0.40 ms | 2.9 s | 9 % |
| **one save: 32 blocks compressed** | `0x005C5FB3` | 113 ms per block | **3.6 s in one go** | 11 % |

The save is the `autosave_history.sav` the game wrote on its own during the run (second run of
the same script; the other rows vary by a few percent between runs). A profile of the same script
(`--profile`, real time) agrees: among the game's own functions, none weighs more than about 1 %
of the run, except the city drawing loop (whose time includes its native passes) and
`0x005C9BE0`, the match search of the save compressor: 318 million blocks, 5.7 s under the
profiler.

### What the review shows

1. **Rewriting emulated code piece by piece no longer pays in speed.** The large lots are done:
   the last ones that showed in real play gave 10–12 % (#109) and 7–9 % (#113) of the CPU.
   What is left in the tick, the drawing and the interface weighs about 1 % of the run or less
   per piece ([simulation §8](engine/simulation.md#8-order-of-the-rewrite),
   [rendering §13](engine/rendering.md#13-order-of-the-rewrite),
   [interface §10](engine/interface.md#10-cost-in-real-time)): below what a player notices, and
   close to the noise of a measure.
2. **Saves freeze the game.** A save compresses its 32 blocks in emulated code, PKWARE DCL
   `implode` (`0x005C96C0`, [formats/sav.md](../formats/sav.md)): 3.6 s of a cloud core in one
   go, while the game stands still. It happens on every save made by hand, at the autosaves the
   game writes on its own (`autosave_history.sav` in this run), and at each month change when
   the game's autosave option is on (`last.sav`, call at `0x00483DE1`; the meaning of the flag
   `0x00F2A7CA` is a hypothesis). The project already has a compressor written by hand that
   gives the game's streams byte for byte (`tools/savefile.py`, 7,008 blocks rewritten
   identical; it keeps the working structure those streams depend on): the largest gain a
   player would notice, and a bounded one.
3. **The cloud does not see the Mac's own costs.** Every figure above comes from a headless
   Linux machine. In the application, each presented frame is also handed to SDL as RGB 555,
   which SDL 2's Metal renderer does not take as is (it converts every frame in software:
   hypothesis, not measured), then scaled for the Retina screen and synchronised with it. One
   run on a Mac tells whether the presentation deserves a lot.
4. **2.0 is out of reach at this pace, and Linux and Intel no longer wait for it.** 24 functions
   are native and 322 understood out of 4,872; most of the code left is the simulation's, and
   its pieces now give no speed. The host itself is portable already: it builds and plays the ten
   scenarios on Linux (the bench runs there), its dialogs (`dialogs_sdl.cpp`) and its JPEG
   decoding (FFmpeg, #38) have portable versions, and no file of `src/` is specific to arm64.
   Linux (#40) and Intel Macs (#41) are packaging, not engine work.

### Next steps, in order

Steps 1 and 2 touch separate files and can run side by side; 2 and 3 share the release
workflow, so one after the other.

| # | Step | Gain in real play | What proves it | Done when |
|---|---|---|---|---|
| 1 | Save compression in native code: `implode` (`0x005C96C0`) ported from `tools/savefile.py`, with its working structure | the pause of every save, 3.6 s of a cloud core, down to a few milliseconds | `--verify 0x005C96C0` on every call of the scenarios that save; their saves identical to the references byte for byte; frames and state identical in the ten scenarios; `--time-span 0x005C5FB3:0x005C5FB8`, `main` and the branch side by side | merged; a save in the application without a visible pause |
| 2 | Intel Macs (#41): the release also built on a GitHub Intel runner, as a second archive or one universal application | players with an Intel Mac under macOS 14 | `--selftest` and the unit tests on the Intel runner; the Intel build opened under Rosetta on an Apple Silicon Mac; a tester with an Intel Mac | an Intel build attached to a release |
| 3 | Linux (#40): an AppImage built by the release workflow, x86-64 first | players on Linux | `--selftest` in CI; the frame log of the `tutorial` scenario equal on Linux and macOS, except the JPEG backgrounds of #38; a Linux player | an AppImage attached to a release |
| 4 | Release 1.3, played through on a Mac as before, with one measuring run of the application in its window (the command of the table above, without `--headless`) | players get step 1, and 2 or 3 if ready | played on a Mac; the shares of the table measured there | published |
| 5 | Presentation on the host, **only if** step 4 shows it above 5 % of the Mac's CPU: frames read straight from the emulator's memory instead of three copies through Unicorn, and a texture format the Mac takes as is | at most the share step 4 measures; 0.4 ms per turn on the cloud machine | frame logs identical in the ten scenarios; CPU of `main` and the branch side by side, on the Mac | merged, or dropped with the figure that rejects it |

### Stopped, and why

- **Simulation** (16 October): the lots left (`0x0040A4C0`, `0x0048CDB0`, `0x005B4B40`,
  `0x005B5620`, `0x004AB210`: 3 to 9 ms each over the `working-city` run) weigh under 1.4 % of
  the run each; walker movement in native code was tried without gain.
- **Rendering**: what is left of [rendering §13](engine/rendering.md#13-order-of-the-rewrite)
  weighs under 0.1 % each; it would matter only for 2.0.
- **Interface**: after #113 every piece is under 4 % of the run; the drawing of panel lines
  (`0x004F58C0`) was ruled out, its gain invisible in real play.
- **Skipping unseen frames** (#36): faithful, but at most 3 % of the work, and a net loss once
  the state copies are counted.
- **Small functions one by one**: a native call from the emulator costs about 140 ns, more than
  about 15 emulated instructions; only a larger caller pays.
- **2.0, no emulator**: on hold. Phase C resumes for a lot that shows a gain in real play, or if
  2.0 becomes the goal again; the code map, the subsystem pages and the bench stay ready for it.

## Where we stand (review of 15 October 2026)

One day after 1.1, Phases A and B are in place and Phase C has started with the rendering.

- **Instruments.** The game state is compared tick by tick, and any native function call by
  call ([equivalence.md](equivalence.md)); every function of the executable is on the code map
  with its module and status ([engine/README.md](engine/README.md)).
- **Data.** Saved games (219 files), maps (57) and adventures (`.set`, `.pak`) are read and
  written back byte for byte, texts too; tables, building plans and event messages are read,
  the tables checked against a re-implementation of the game's own parser.
- **Engine.** Three subsystem pages (rendering, interface and texts, terrain), each ending with
  the ordered list of functions to rewrite. The city drawing is largely native: 8 functions
  replaced whole, 195 understood out of 4,872, and 48.7 % of the game's computation done
  natively on the large city (+ 14.7 % in functions that are partly native).

What the review shows:

1. **The rest of the drawing is about completeness, not speed.** The last pixel routines changed
   nothing measurable in real time (#88). A small city is at about 23 % of a core; the large city,
   at 84 % in 1.1, lost about a third of its time per tick since, still well above the 35 % of
   #22.
   Its remaining cost is in the walkers, the building extras and the simulation: a hypothesis
   until it is measured (step 5 below).
2. **2.0 is mostly the simulation, and its size is not known yet.** 4,131 functions, 76 % of the
   executable's code (1.09 MB of 1.43 MB), belong to no module so far; rendering, interface,
   texts and terrain together are 10.5 % of it. The native share of the computation grows fast
   because drawing is concentrated in a few functions; the count of functions understood grows
   slowly.
3. **The bench cannot prove what the scenarios never call.** Rendering steps 5 to 7 (glyph
   masks, overlays, rare blends), the base of 6 × 6 tiles and every overlay branch are reached by
   none of the nine scenarios.
4. **What costs the most time is around the rewrites, not in them.** Setting up the bench on a
   fresh machine (game files, references of the pure original, nine scenarios) costs more than
   most rewrites, and it is slow on Linux: Ubuntu 24.04 ships Unicorn 2.0.1, which cannot use
   non-executable pages, so every memory write takes the emulator's slow path
   (`src/win32/cpu.cpp`, `Cpu::enable_paging`). Branches that edit the same lines (counters, the
   "where the list stands" paragraphs, the table of native functions) cost rebases. Work that
   lived on one machine only was nearly lost twice.

### Next steps, in order

Steps 2, 4 and 5 can run side by side: each touches its own files.

| # | Step | Why now | Done when |
|---|---|---|---|
| 1 | Finish the rendering in progress: bases of 2 to 6 tiles (#91), walker sort, walkers ([rendering §13](engine/rendering.md#13-order-of-the-rewrite), steps 10, 12 to 14) | already written, verification under way | merged; frames, state and call-by-call checks identical. **Done (16 October), except rendering steps 5 to 7, which wait for step 4:** bases (#91), sprite sort and dispatcher groundwork (#95, #96) merged; building extras (#99) and walker loops (#101) merged; only the rendering steps 5 to 7 remain (step 4) |
| 2 | A fast, repeatable bench on Linux: Unicorn 2.1 or later, references of the pure original recorded once per change of the Windows layer and reused | every later step runs the bench several times | the nine scenarios recorded and replayed on a fresh Linux machine with one command. **Done (#93)** |
| 3 | Release 1.2 | players get the native drawing (a small city from about 35 % to about 23 % of a core) | played through on a Mac, as for 1.0. **Done (16 October): [v1.2](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v1.2) published from `main` after #111, played on a Mac; its archive is now built by the CI (`release` workflow)** |
| 4 | A tenth scenario for the rare drawing cases (a building marked red, glyph masks, the rare blends, a 6 × 6 base), then rendering steps 5 to 7 | no proof is possible without it, and 2.0 needs them | the scenario reaches each of those functions; steps 5 to 7 native and checked call by call |
| 5 | Map the simulation: a profile of the large city in real time with native code on, and the call tree of the simulation tick placing functions in modules | sizes 2.0 and orders the rest of Phase C by measured cost | every function the simulation tick reaches in the nine scenarios has a module; a page `docs/engine/simulation.md` gives the order of a tick and what each module costs. **Done (#94)**: 537 of the 601 functions specific to the tick have a module |
| 6 | Terrain in native code, steps 1 to 4 of [terrain §10](engine/terrain.md#10-order-of-the-rewrite) (random draw, diamond tests, neighbour counts, free building area) | documented and self-contained; the first native code of the simulation, whose results are the game state itself, so the first real test of the tick-by-tick comparison | steps 1 to 4 native; state identical at every tick of every scenario. **Done (#97)** |
| 7 | The other subsystems in the order step 5 gives; interface and texts once the sprite dispatcher is native whole | their value is fidelity more than speed ([interface §10](engine/interface.md#10-cost-in-real-time)) | — **Stopped (16 October):** composite-building helpers (#98), road search (#100), walker loop of the tick (#102), soldiers (#104, #105) and cycle (#108, #110) merged, interface texts and frames (#107, #109, #113) too; each lot left weighs about 1 % of the run or less ([after 1.2](#after-12-review-of-16-october-2026)) |

### Progress since the review (16 October 2026)

Merged: #91 (bases of 2 to 6 tiles), #93 (fast bench), #94 (simulation map), #95 and #96 (walker
drawing, aquatic life, walker sort), #97 (terrain), #98 (composite-building helpers), #99
(building extras of the drawing), #100 (road search), #101 (walker loops of the drawing), #102
(walker loop of the simulation), #104 and #105 (soldiers), #106 (tenth scenario `rare`), #107
(panel texts), #108 and #110 (cycle), #109 (window frames), #111 (overlay, blends and glyph
masks: rendering steps 5 to 7). Version 1.2 published on 16 October.

Measured, as the pull requests give it (`working-city`, deterministic clock, `main` and branch
side by side on one cloud machine):

- Drawing: second pass 84 % of tiles fully native (81 % before) with #91; 92 % (84 % before) with #99.
- Bench: Unicorn 2.1.4 replays the `options` scenario in 174 s against 865 s with 2.0.1; a fresh
  clone records 9 references and replays 9 scenarios in 14 min 26 s, then 2 min 54 s per replay.
- Simulation tick of the large city: 4.36 ms, 81 % in the walkers (#94). Composite-building
  helpers: 5.3–5.8 ms to 3.1–3.3 ms (about 40 % less, #98). Road search: 2.68–2.74 ms to
  2.38–2.52 ms (about 10 %, #100). Walker loop: 2.49–2.73 ms to 1.85–2.12 ms (#102).
- Terrain (#97): no speed gain, by design. Intercepting the two diamond tests alone made `poseidon`
  12 % slower (35.5 s against 31.3 s) because a native call from the emulator costs about 140 ns;
  they are native only inside `0x004A0DC0`.
- #95 and #96 give little speed (0.2–0.3 % of the profile each); their value is to open the walker loops.

How the work is checked, from now on:

- Counters (`docs/engine/README.md`, the README) and the "where the list stands" paragraphs are
  refreshed in one change after a batch of merges, not in every pull request.
- Real-time figures in a pull request compare `main` and the branch on the same machine at the
  same time; absolute figures (share of a core) are measured on a Mac, before a release.
- Work in progress lives on a pushed branch from its first commit.

## What does not change

These rules hold for every step; they are what makes the project trustworthy.

- **The original is the reference.** It keeps running, emulated, as the judge: a rewritten part
  is accepted only when it gives exactly the same game state, turn by turn, on replayable
  scenarios and on real saves.
- **Written by hand, documented.** No mechanically translated code. Each subsystem gets a page
  in `docs/engine/`, the native code and its proof of equivalence.
- **Nothing from the game is distributed.** The repository and the releases contain only this
  project's code. Whatever is derived from the game — language packs, data read from the
  player's own files — is produced on the player's machine, from the player's own copy.
- **`main` stays the original game.** Only what changes nothing in the original game enters
  `main`; modding comes later, on a branch.
- **No copy protection is touched.** Only the DRM-free GOG edition is run.

## 1.x — Playable

The game works. This line keeps it comfortable: first launch in few clicks ([#55](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/55)), language packs
for every edition ([#24](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/24), testers needed), and less processor use ([#22](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/22)), which becomes a side
effect of Phase C rather than a goal of its own.

## Phase A — Instruments

Before rewriting anything, the means to prove it.

- **Equivalence harness** ([#26](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/26), done): the whole game state compared tick by tick between the
  pure original and the host, and one game function run original and native from the same
  state with registers and memory compared ([equivalence.md](equivalence.md)).
- **Code map and counters** ([#69](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/69), done): each of the 4,872 functions of the GOG executable filed under a module, and two
  public numbers — functions understood out of 4,872, and the share of the computation done
  natively ([engine/README.md](engine/README.md)). Most functions have no module yet: placing
  them is step 5 of the next steps above.

## Phase B — Data

The game's files, described completely and rewritten byte for byte.

- **Savegame** ([#70](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/70)): the `.sav` file described in full, with a reader and a writer.
- **Then** maps and adventures, images, texts and tables ([#30](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/30)); sprites, maps and texts are
  already partly done.

**Done when** each format read and then written back gives the identical file.

**Where it stands.** Saved games, maps, adventures (`.set`, `.pak`) and texts are read and
written back byte for byte; the `Model` tables, building plans, event messages and images are
read. The engine only reads files, so this is enough for Phase C: writing images and tables back
moves to the modding branch, where it is needed, and the meaning of each savegame block is filled
in as Phase C reaches its subsystem.

## Phase C — Native engine by subsystem

One parent issue ([#71](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/71)) lists the subsystems: rendering, interface and texts, terrain, movement
and walkers, housing and buildings, economy, gods, heroes and monsters, army, events and
adventures, savegame. For each: a page in `docs/engine/`, the native code, and the proof of
equivalence against the emulated original. Lower processor use ([#22](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/22)) comes along. The rendering
is largely native; what follows is ordered in [After 1.2](#after-12-review-of-16-october-2026).

## 2.0 — Native engine

**No emulator to play.** Once the engine is complete, the game runs on its own with the player's
own data files. Linux and Intel Macs follow from it ([#25](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/25)); the portable pieces already
started — MP3 decoding is done ([#37](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/37)) — are [#38](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/38) JPEG, [#39](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/39) dialogs, [#40](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/40) Linux build,
[#41](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/41) universal Mac application.

## Modding (branch)

Later, on a `modding` branch ([#72](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/72)). Nothing that changes the original game enters `main`.

## Exploration — the City Building Series

*Emperor: Rise of the Middle Kingdom* was built on the engine of *Zeus*; *Pharaoh* and *Caesar III*
come from the generation before. On hold: measurements first, a milestone only if the answer is
good ([#28](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/28), [#29](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/29)).

## Ongoing — the game's knowledge, written down

Every file format documented with a reference reader ([#30](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/30)). This knowledge outlives any port
and serves modders and tool makers.

## Decisions for the maintainer

- **A signed and notarised application** would remove the *Open Anyway* steps on first
  launch. It needs a paid Apple Developer membership; under consideration.

---

## Looking back on 1.0

**What worked.**

- *Running the original code instead of rewriting it.* An earlier plan rewrote the engine module
  by module; switching to an emulator with a native Windows layer turned an open-ended rewrite
  into a playable game in a few milestones, with fidelity for free.
- *Letting the game say what it needs.* A Windows function that is not written stops the program
  and names itself: 199 of the 202 imports were written that way, in the order the game asked
  for them. The last three are never reached
  ([details](reverse-engineering.md#findings-in-the-gog-executable)).
- *Proving every change.* The deterministic scenarios turned "it seems to work" into "these
  38,593 frames are identical", which made bold optimisations safe.

**What we learned.**

- *Measure before optimising, and check the measure.* An early profiler overstated short loops;
  the first native functions brought nothing; the 3.5-second start-up turned out to be one loop
  of 18 million string comparisons; and the clock calls that looked like a busy wait were the
  game's own profiling timers.
- *Deterministic tests do not see time.* The scenarios run on a virtual clock, so they could not
  notice that the host's pacing slowed the game down to two thirds of its speed from 0.2 to 1.0.
  Real-time measurements of ticks per second now complement them.
- *Emulator overhead hides in unexpected places.* Without paging, every memory write took the
  emulator's slow path; mapping data as non-executable gave a fourfold speed-up.
- *Releases need the same care as code.* Two early archives carried build paths of the machine
  that made them; packaging now strips and checks every byte.

**Risks we keep an eye on.**

- One Mac model and two language editions tested so far: reports from other setups matter more
  than any new feature.
- The project depends on Unicorn; its x86 emulation and its performance are outside our hands
  until 3.0.

## History

| # | Milestone | Visible result | State |
|---|---|---|---|
| 0 | Scoping | DRM-free GOG executable identified and analysed | done |
| 1 | The game starts | the real main menu in a Mac window | done |
| 2 | Playable | mouse, keyboard, load and save, a tutorial can be played | done |
| 3 | Complete | music, sound effects, intro video, full screen | done |
| 4 | Ready to share | performance, self-contained `.app`, automated builds, first release | done (0.1) |
| 5 | French edition | French texts and voices from the player's own CDs, wide screens | done (0.2, 0.3) |
| 6 | Native code | drawing, start-up and the sprite dispatcher in native code | partly done since 0.4; continues as 3.0 |

### Releases

| Version | Highlights |
|---|---|
| [1.2](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v1.2) | lighter: second drawing pass, walkers, window frames and texts in native code; simulation tick of the large city about three times faster; ten scenarios checked tick by tick |
| [1.1](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v1.1) | easier first launch; taller wide modes; native ground drawing; Linux build in CI |
| [1.0.1](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v1.0.1) | the game at its real speed; native sprite dispatcher; this roadmap |
| [1.0](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v1.0) | first stable version: the code of 0.9, played through by hand |
| [0.9](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v0.9) | release candidate: start-up under a second, real resolution shown in wide mode, file cache limited to small files |
| [0.4](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v0.4) | 3D sound; six native routines and 100 ns system calls; mouse wheel; pause in the background; scenarios for Poseidon, the editor, the options and a large city |
| [0.3](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v0.3) | wide screen; language packs built by the application from CD images; stripped and checked bundle |
| [0.2](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v0.2) | French; non-executable data pages and a paced loop (half the CPU); stop report; GOG installer opened by the application; LGPL FFmpeg |
| [0.1](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v0.1) | first preview: the original game in a sandboxed emulator, menus, tutorials, building, save and load, music, sound, intro video, full screen, application bundle |

The archives of 0.1 and 0.2 were withdrawn: they carried build paths of the machine that made
them. Every release and pull request is listed in [CHANGELOG.md](../CHANGELOG.md).

### What 1.0 was checked against

- By hand: adventures of Zeus and Poseidon, 3D sound by ear, the pause when switching to
  another application with ⌘Tab, the mouse wheel, a French pack built by the application from
  the original CDs, in window and full screen, on a MacBook Air M4.
- Eight replayable scenarios (`tests/scenarios`): first tutorial, first Zeus adventure with
  every tab of the control panel, first Poseidon adventure, the adventure editor, every window
  of the *Options* menu and the in-game help, wide mode, background pause and mouse wheel, the
  large city of the last tutorial. Identical with and without the native functions.
- Each of the sixteen tutorials loaded in turn and its screen reviewed.
- Endurance: the large city at full game speed for 25 minutes (several game years, autosaves,
  an earthquake, a god and a monster in the streets); 20 minutes with memory sampled every
  minute (flat, about 530 MB); 10 minutes with the released application, a French pack and
  wide mode.
- Every sound file of the game (703 WAV, 929 MP3, 311 minutes) decodes through the code paths
  of the mixer (`zeus-run --check-audio`); so do the files of a French pack.
- Save and load in a Poseidon adventure with the French pack, under an accented name; the file
  is read to the last byte by `tools/mapfile.py`.

### Known limits

- Sound: the fall-off of 3D ambience with distance is an assumption (linear, between Miles's
  default distances).
- Video: audio and picture are paced independently, so they may drift slightly.
- The game's *Windowed screen* option is refused by the game itself (it compares the screen
  with the mode, and the host reports a screen the size of the mode); ⌘F switches between
  window and full screen instead.
- The world map image is 838×738: in a wide mode, wider or taller than 1024×768, the city stays
  visible to its right and below it ([#23](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/23)).

### Native code: how it works

**A rule first.** Code translated mechanically from the game's executable is still the game's
code: it cannot be kept in this repository or shipped in a release. Native code here is
written by hand from an understanding of what a function does, and proven against the original
with the frame logs of deterministic runs. Mechanical translation (Ghidra p-code,
`tools/ghidra/ExportPcode.java`) is only a reading aid. Version 3.0 keeps the rule by running
its translator on the player's machine.

**How a native function is entered.** The first byte of the game's function is replaced in
memory by a plain return, and the host function runs just before it, from a hook. The emulator
is never diverted, so entering costs about 75 ns. System calls use the same trick: each import
slot holds a `ret n`, which brought a system call from 4,500 ns down to about 100 ns.
`zeus-run --bench` measures both. For scale, copying one 58×30 tile takes 4,700 ns emulated and
130 ns natively. A loop can be replaced on its own (its first instruction becomes a jump to its
exit), and a function can get a native fast path that falls back to the original code
(`Cpu::add_fast_path`).

**Where the time goes.** `zeus-run --profile` measures time per block of game code and
`tools/profile_report.py` sums it per function, subtracting the cost of the measurement itself.

**What is native today** (`src/win32/native.cpp`, `native_draw.cpp` and `native_walkers.cpp`, on by default, `--no-native` to compare):

| Game function | What it does |
|---|---|
| `0x00413DC0` | copies an opaque rectangle to the frame buffer |
| `0x00416010` | same, with magenta as the transparent colour |
| `0x004161B0` | draws a run-length compressed image, optionally as a shadow |
| `0x004163B0`, `0x004165D0` | the same, clipped on the left or on the right |
| `0x00415490`, `0x004158E0`, `0x00415680` | the same, mirrored, whole or clipped |
| `0x00415F50` | copies whole rows with magenta as the transparent colour |
| `0x00413E30` | draws a run-length compressed image blended with the background |
| `0x0050B410` | clears the frame buffer |
| `0x005CC640` | `_strcmpi` from the C runtime |
| `0x005059F8` | the loop that looks up each key of `Model\Zeus eventmsg.txt` at start-up (18 million `_strcmpi` calls): start-up went from 3.5–5 s to under a second |
| `0x005ACF50` | the sprite dispatcher, for its common cases (run-length images not mirrored, clipped or not; opaque images and ground tiles not clipped sideways), with its clipping (`0x00509C20`, `0x00509CD0`); every other case falls back to the original |
| `0x005ADF00` | the top of a building, the compressed part above its base, placed by the width of the base; with an overlay colour it falls back to the original |
| `0x00435070` | draws a ground tile, a 58 × 30 diamond (`src/win32/native_draw.cpp`) |
| `0x004377B0`, `0x00438A90` | the same, clipped on the left or on the right (a fixed half of the diamond, as the original does) |
| `0x0043FF10`, `0x00440170`, `0x004406B0`, `0x00440FE0`, `0x00441E50` | the base of a building of 2 to 6 tiles, diamond by diamond (inside the sprite dispatcher) |
| `0x00457900` | the ground of one city tile, for its common cases: flat or sloped land in the normal view, animated water included |
| `0x0044FEF0` | one walker: its place in the tile, its one to three sprites in the right order, and the higher neighbouring tiles marked to be drawn again over it (`src/win32/native_walkers.cpp`); the C++ objects of sanctuaries and the trader's text fall back to the original |
| `0x0041D3D0` | the water life of a tile, when there is none to draw (always outside a Poseidon game) |
| `0x00449B30` | sorts the walkers of a tile from the back to the front before they are drawn |
| `0x0045A5A0` | sorts and draws the walkers of a tile, like the two loops of the second pass for the rows behind it, which now run natively too |
| `0x00458170` | the city drawing loop: its first pass over the visible tiles (the ground), and its second pass (what stands on each tile) for 97 % of the cells of a large city, through host jumps that hand back to the original, mid-cell if needed, for any other case |

All nine scenarios (43,159 frames, 13,073 simulation ticks) give the same frames and the same game state, tick by tick, with and without them, and each of them passes the call-by-call check of [equivalence.md](equivalence.md).

**Also on the host side.** The game reopens the same small files thousands of times a minute;
read-only files of 4 MB or less are kept in memory and directory listings are cached.

### Wide screens

`zeus-run --wide auto` (always on in the application) replaces the game's 1024×768 mode by a
wider one that matches the proportions of the screen, for instance 1204×768 on a 16:10 display
and 1324×768 on a 16:9 one. Choose the largest resolution in *Options → Display options*: the
map view grows, the control panel stays at the right edge. Menus and briefing screens are drawn
by the game in 800×600 whatever the mode. The labels of the display options and of the F7 key
show the real size: the host serves the game a copy of its text files where "1024 x 768" is
replaced, at equal length. How it works is described in
[reverse-engineering.md](reverse-engineering.md).

### Earlier work

Before the current approach, the project decoded the sprite and map formats and ported the
terrain renderer natively (`zeus-sprites`, `zeus-map`). That code is kept as a reference. One
known defect remains in the native terrain renderer (black diamonds at the foot of some cliffs).
