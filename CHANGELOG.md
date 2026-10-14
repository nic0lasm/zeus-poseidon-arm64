# Changelog

Notable changes, most recent first. Each entry of the "unreleased" section corresponds to a
pull request.

## Unreleased

## 0.3 — 2026-10-14

- **Language packs from the application.** Pick a folder with the images of the original CDs:
  the application builds the pack itself, with its own Python interpreter. Hold Option at
  launch to choose the language again. `zeus-run --make-pack` does the same from the command
  line.
- **Wide screen.** `--wide auto` (always on in the application) turns the game's 1024×768 mode
  into a wide mode fitted to the screen: larger map view, control panel at the right edge.
- `--watch-call` and `--dump-data`, two reverse-engineering aids.
- Replayable scenarios (`tests/scenarios`, `tools/check_scenarios.py`): a tutorial and the
  first adventure, compared frame by frame against a recorded reference.
- `CompareStringW` is provided.
- Guest memory owned by the host; first game functions rewritten natively, behind `--native`
  (no speed gain measured yet).
- Time-based profiler (`--profile`) and `tools/profile_report.py`.
- File lookups in the game folder are cached.
- Deterministic runs (`--deterministic`) and per-frame hashes (`--frame-log`), to prove that a
  change does not alter the game.

## 0.2 — 2026-10-14

- **French.** Language packs built from another edition of the game: texts, fonts,
  adventures, voices. The host adapts the character table and the placement of accented
  letters of the GOG executable in memory. Accented names can be typed. The application offers
  the packs found in its `Packs` folder.
- **Performance.** The emulated processor runs with non-executable data pages, which removes
  the emulator's slow path for memory writes; the game loop is paced at 60 turns per second.
  A running city uses about half of one CPU core instead of a full one.
- A message and a short report when the game stops on a missing Windows function.
- The application opens the GOG installer directly and unpacks it.
- A redistributable bundle: FFmpeg reduced to the Bink decoders (LGPL), licence notices.
- Automated build, issue templates, code of conduct.

## 0.1 — 2026-10-14

First preview.

- The original game runs inside a sandboxed x86 emulator, with the Windows functions it
  imports rewritten natively: files, window, mouse, keyboard, DirectDraw.
- Menus, leader profiles, tutorials, building, saving and loading.
- Music, ambience and sound effects (Miles Sound System on a native mixer); intro video (Bink).
- Window or full screen, sharp at any size.
- Self-contained application bundle.
- Tools and documentation from the study of the game: sprite and map formats, extraction of
  the original CDs, reverse-engineering notes.
