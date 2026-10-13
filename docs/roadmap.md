# Roadmap and status

## Milestones

| # | Milestone | Visible result | State |
|---|---|---|---|
| 0 | Scoping | DRM-free GOG executable identified and analysed | done |
| 1 | The game starts | the real main menu in a Mac window | done |
| 2 | Playable | mouse, keyboard, load and save, a tutorial can be played | done |
| 3 | Complete | music, sound effects, intro video, full screen | done |
| 4 | Ready to share | performance, self-contained `.app`, automated builds, first release | done |
| 5 | French edition | French texts and voices from the player's own CDs; wider resolutions | next |
| 6 | No emulator | static recompilation of the game's code to native arm64 | planned |

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
- A hands-on play session of several thousand frames without hitting a missing function.

## Known limits

- Sound: reverb, filters and 3D positioning are ignored.
- Video: audio and picture are paced independently, so they may drift slightly.
- **Performance**: a running city uses about half of one CPU core; loading screens use a full
  core. The game itself caps its display rate at 20 frames per second.
- `CompareStringW`, `RaiseException`, `RtlUnwind` and several DirectDraw methods are not
  written; no play session has needed them so far.
- Tested with the English GOG edition only.

## Milestone 5 — plan

1. ~~Check that French files can be layered over the GOG executable~~ — done: it works, once
   the character table and the placement of accented letters are taken from the French build.
2. ~~Typing accented letters~~ — done. A full French play session: to be reported.
3. ~~Language choice in the application~~ — done.
4. Wider resolutions: the game's own 1024×768 mode works (Options → Display options). Going
   wider is not a setting: see below.

### Wide screens

The game picks its screen size in one place (`0x0050BCC9`–`0x0050BD07`: 640×480, 800×600 or
1024×768, stored at `0x00F2A6B4` and `0x00F2A6B0`). Replacing 1024×768 there by 1280×800 does
switch the mode, but the layout does not follow: the map view stays small and the control panel
sits in the middle of the screen. About 80 places read the screen size and many compare it to
800 or 1024. A wide mode means adapting each of them; it is a project of its own.

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
