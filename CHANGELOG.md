# Changelog

Notable changes, most recent first. Each entry of the "unreleased" section corresponds to a
pull request.

## Unreleased

## 0.4 — 2026-10-14

- **Faster.** Six of the game's drawing and string routines now run natively, system calls no
  longer stop the emulator, read-only game files are kept in memory and directory listings
  are cached. Frame logs of all scenarios are unchanged. `--no-native` emulates everything.
- `--counts` also reports the time spent in each Windows function; the profiler subtracts its
  own overhead.
- **3D sound.** The host offers a software 3D provider: with "3D sounds" on in the game's sound
  options, ambience is placed around the listener. Reverb and filters turn out never to be
  enabled by this build of the game.
- **Mouse wheel.** The wheel (or a two-finger swipe) scrolls the game's lists and texts.
- **Pause in the background.** When the window is no longer in front, the game is told so, as
  under Windows: it silences its sounds and waits, then carries on when you come back.
- **Wider automated coverage.** Four more scenarios: the first Poseidon adventure, the
  adventure editor, every window of the *Options* menu, and the large city of the last
  tutorial. All sixteen tutorials were loaded in turn, and the large city was left running for
  25 minutes at full speed. No missing function turned up.
- `zeus-run --check-audio FOLDER` decodes every sound file the way the mixer does: the 1,632
  files of the game pass. It also showed that one voice file of the French CDs is empty on the
  discs themselves.
- Scripts can drag (`down X,Y`, `up X,Y`), turn the wheel (`wheel N`) and simulate the window
  losing and regaining focus (`blur`, `focus`).
- `tools/ghidra/Listing.java` prints the disassembly of code Ghidra attached to no function,
  such as the window procedure. The notes now describe how the game reads the mouse, list
  every DirectDraw method it calls (all provided) and explain why `RaiseException` and
  `RtlUnwind` are never reached.

## 0.3 — 2026-10-14

- The application bundle is stripped of debugging information, and `tools/make_app.py` refuses
  to produce a bundle that mentions the home folder of the machine it was built on. The
  archives of versions 0.1 and 0.2 carried such build paths and have been withdrawn.
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
