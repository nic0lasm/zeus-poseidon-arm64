# Roadmap and status

## Milestones

| # | Milestone | Visible result | State |
|---|---|---|---|
| 0 | Scoping | DRM-free GOG executable identified and analysed | done |
| 1 | The game starts | the real main menu in a Mac window | done |
| 2 | Playable | mouse, keyboard, load and save, a tutorial can be played | done |
| 3 | Complete | music, sound effects, intro video, full screen | done |
| 4 | Ready to share | performance, self-contained `.app`, automated builds, first release | done |
| 5 | French edition | French texts and voices from the player's own CDs | done; widescreen left open |
| 6 | No emulator | the game's hot code translated to native arm64, then the rest | started |

### Why this order

The game is playable; what stands between it and players is speed and packaging. Profiling a
running city shows the whole frame time is spent inside the emulator, and more than half of
that in its slow memory-write paths rather than in the game's code. Milestone 4 therefore
starts with measurements and emulator tuning, then packaging.

Milestone 6 replaces an earlier idea (rewriting the engine by hand, module by module).
Fidelity is already guaranteed by running the original code; translating that code
mechanically to native code keeps the guarantee, removes the emulator and its overhead, and
is far less work than a rewrite.

## What has been verified

- Clean build with `-Wall -Wextra -Wpedantic`; `zeus-run --selftest` passes.
- Scripted run (no window): *Play Zeus*, typing a leader name, *Tutorials*, *Housing and
  People*; the city is alive, the calendar advances, a housing lot placed next to a road
  becomes a house and the population rises; arrow keys scroll the map; *Save adventure* writes
  a save file that the reference reader (`tools/mapfile.py check`) reads to the last byte;
  *Load adventure* lists it; *Quit* exits cleanly.
- Sound, scripted run: menu music, button clicks, music fade-out, city music, ambient loops;
  the mixer output is non-silent.
- Video, scripted run: the intro plays at 24 frames per second in 640×480, Esc skips it, and
  the main menu follows in 800×600.
- Hands-on play sessions of tens of thousands of frames, in English and in French, without
  hitting a missing function.
- Scripted soak run in the first adventure (*Zeus and Europa*): every tab of the control
  panel, building attempts, 19,566 frames and 2.2 million system calls without a stop.
- Scripted runs of the first Poseidon adventure (a road drawn by dragging, housing, world map),
  of the adventure editor (adventure screen, city map, terrain brush), of every window of the
  *Options* menu, of the in-game help; each of the sixteen tutorials loaded in turn and its
  screen reviewed.
- Endurance: the large city of the last tutorial, 25 minutes in real time at full game speed,
  several game years with autosaves, an earthquake, a god and a monster walking the streets,
  without a stop.
- Every sound file of the game (703 WAV, 929 MP3, 311 minutes) decodes through the code paths
  of the mixer (`zeus-run --check-audio`); so do the files of a French pack.
- The seven scenarios of `tests/scenarios` give the same frames with and without the native
  functions.

## Known limits

- Sound: the fall-off of 3D ambience with distance is an assumption (linear, between Miles's
  default distances); the original providers may have sounded quieter or louder.
- Video: audio and picture are paced independently, so they may drift slightly.
- **Performance**: a running city uses about half of one CPU core; loading screens use a full
  core. The game itself caps its display rate at 20 frames per second.
- `RaiseException` and `RtlUnwind` are not written: the game only reaches them after a
  processor fault, which the host reports instead
  ([details](reverse-engineering.md#findings-in-the-gog-executable)). Every DirectDraw method
  the game calls is provided.
- The 3D placement of ambience has been checked by measurement only, not by ear; reports are
  welcome.
- Tested with the English GOG edition, alone and with a French pack, on a single Mac model.

## Milestone 6 — in progress

**Goal.** Run the game's hot code natively and leave the rest to the emulator, then decide
whether the emulator can go.

**A rule first.** Code translated mechanically from the game's executable is still the game's
code: it cannot be kept in this repository or shipped in a release. Native code here is
written by hand from an understanding of what a function does, and proven against the original
with the frame logs of deterministic runs. Mechanical translation (Ghidra p-code,
`tools/ghidra/ExportPcode.java`) is only a reading aid.

**How a native function is entered.** The first byte of the game's function is replaced in
memory by a plain return, and the host function runs just before it, from a hook. The emulator
is never diverted, so entering costs about 75 ns. System calls use the same trick: each import
slot holds a `ret n`, which brought a system call from 4,500 ns down to about 100 ns.
`zeus-run --bench` measures both. For scale, copying one 58×30 tile takes 4,700 ns emulated and
130 ns natively.

**Where the time goes.** `zeus-run --profile` measures time per block of game code and
`tools/profile_report.py` sums it per function, subtracting the cost of the measurement itself
(an earlier version did not, and overstated short loops). On the reference scenario the picture
before any native code was: opaque rectangle copy 12.5 %, run-length sprite drawing 12.4 %, the
sprite dispatcher 11 %, system calls 10 %, the city drawing loop 8.7 %, `_strcmpi` 5 %.

**What is native today** (`src/win32/native.cpp`, on by default, `--no-native` to compare):

| Game function | What it does |
|---|---|
| `0x00413DC0` | copies an opaque rectangle to the frame buffer |
| `0x00416010` | same, with magenta as the transparent colour |
| `0x004161B0` | draws a run-length compressed image, optionally as a shadow |
| `0x00413E30` | draws a run-length compressed image blended with the background |
| `0x0050B410` | clears the frame buffer |
| `0x005CC640` | `_strcmpi` from the C runtime |

All three scenarios (16,878 frames) give the same frame logs with and without them.

**Also done on the host side.** The game reopens the same files thousands of times a minute;
read-only game files are now kept in memory and directory listings are cached, which cut the
time spent opening files by two thirds.

**Next.** The sprite dispatcher (`0x005ACF50`) and the city drawing loop (`0x00458170`), which
now top the profile, and the function that calls `_strcmpi` 18 million times in 90 seconds.

## Milestone 5 — plan

1. ~~Check that French files can be layered over the GOG executable~~ — done: it works, once
   the character table and the placement of accented letters are taken from the French build.
2. ~~Typing accented letters~~ — done. ~~Packs built by the application from CD images~~ —
   done.
3. ~~Language choice in the application~~ — done.
4. ~~Wider resolutions~~ — done: see below.

### Wide screens — done

`zeus-run --wide auto` (always on in the application) replaces the game's 1024×768 mode by a
wider one that matches the proportions of the screen, for instance 1324×768 on a 16:10
display. Choose *Options → Display options → 1024 x 768 resolution* in the game: the map view
grows, the control panel stays at the right edge. Menus and briefing screens are drawn by the
game in 800×600 whatever the mode, and are unaffected. Nothing is changed on disk.

How it works is described in [reverse-engineering.md](reverse-engineering.md). Two limits:

- The label in the game still reads "1024 x 768".
- The control panel artwork is 768 pixels high. A taller mode (`--wide 1444x903`) works but
  leaves a black band under the panel, which is why `auto` keeps the height at 768.

The game's "Windowed screen" option answers "Cannot set Windows mode" and carries on; use ⌘F
to switch between window and full screen instead.

## Milestone 4 — done

1. Performance: reproducible measurement (frames, loop iterations, input latency), then
   emulator tuning — done. In a fixed 95-second scripted run the game loop went from about
   7,900 turns to 35,000 with an identical game state, by mapping only code as executable
   and running the guest with non-executable data pages. The loop is now paced at 60 turns
   per second; a running city uses about half of one core instead of all of it. A system call
   costs about 4 µs, which is negligible.
2. ~~A report file and a clear message when the game stops on a missing function~~ — done.
3. ~~A self-contained application bundle that asks for the game folder on first launch~~ —
   done (`tools/make_app.py`), for personal use.
4. ~~A redistributable build: FFmpeg reduced to the Bink decoders under LGPL terms, so that
   the bundle can be published alongside Unicorn (GPL-2.0)~~ — done (`tools/build_ffmpeg.py`).
5. ~~Automated build on every change~~ — done. ~~A first published release~~ — done (v0.1).

## Earlier work

Before the current approach, the project decoded the sprite and map formats and ported the
terrain renderer natively (`zeus-sprites`, `zeus-map`). That code is kept: it is the useful as a
reference for milestone 6. One known defect remains in the native terrain renderer (black diamonds
at the foot of some cliffs).
