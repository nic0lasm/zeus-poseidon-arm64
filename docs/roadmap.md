# Roadmap and status

## Milestones

| # | Milestone | Visible result | State |
|---|---|---|---|
| 0 | Scoping | DRM-free GOG executable identified and analysed | done |
| 1 | The game starts | the real main menu in a Mac window | done |
| 2 | Playable | mouse, keyboard, load and save, a tutorial can be played | done |
| 3 | Complete | music, voices, sound effects, videos, full screen, `.app` bundle | in progress |
| 4 | Going native | original code replaced piece by piece, rendering first | planned |

## What has been verified

- Clean build with `-Wall -Wextra -Wpedantic`; `zeus-run --selftest` passes.
- Scripted run (no window): *Play Zeus*, typing a leader name, *Tutorials*, *Housing and
  People*; the city is alive, the calendar advances, a housing lot placed next to a road
  becomes a house and the population rises; arrow keys scroll the map; *Save adventure* writes
  a save file that the reference reader (`tools/mapfile.py check`) reads to the last byte;
  *Load adventure* lists it; *Quit* exits cleanly.
- Sound, scripted run: menu music, button clicks, music fade-out, city music, ambient loops;
  the mixer output is non-silent.
- A hands-on play session of several thousand frames without hitting a missing function.

## Known limits

- **No video** yet. Sound works (music, ambience, effects) but reverb, filters and 3D
  positioning are ignored, and it has been checked by measurement more than by ear.
- **Performance**: one CPU core is always busy and input can lag slightly. The game's own loop
  never sleeps and the emulator is the bottleneck. The game itself caps its display rate.
- `CompareStringW`, `RaiseException`, `RtlUnwind` and several DirectDraw methods are not
  written; no play session has needed them so far.
- Windowed 800×600 only.
- Tested with the English GOG edition only.

## Milestone 3 — plan

1. ~~Miles Sound System: sound effects (`.wav`) and streamed music (`.mp3`)~~ — done; voices
   and event sounds still to be checked in play.
2. Bink: intro and event videos.
3. Performance: idle the CPU between frames, measure input latency.
4. Full screen and integer scaling.
5. An application bundle that asks for the game folder on first launch.

## Earlier work

Before the current approach, the project decoded the sprite and map formats and ported the
terrain renderer natively (`zeus-sprites`, `zeus-map`). That code is kept: it is the starting
point of milestone 4. One known defect remains in the native terrain renderer (black diamonds
at the foot of some cliffs).
