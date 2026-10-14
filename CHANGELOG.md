# Changelog

Notable changes, most recent first. Each entry of the "unreleased" section corresponds to a
pull request.

## Unreleased

- **`Model/*.txt` documented.** `formats/model.md` describes the difficulty tables (buildings and houses, figures and enemies, tax sentiment, monster strength) and how the game reads them; `tools/model.py` reads all 63 files of the game and checks them against a re-implementation of the game's own parser.
- **Download in one click, honest first-launch steps.** The README (English and French) opens with a direct link to `releases/latest/download/Zeus-Poseidon-macos-arm64.zip` (`tools/publish.py release` now also writes the archive under that version-free name, same bytes), gives the exact macOS 15 steps (*Privacy & Security → Open Anyway*; right-click → *Open* no longer works), says where to get the GOG installer from a Mac, and that an Apple Silicon Mac is required ([#55](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/55)).
- **zeus-run split by role.** `zeus_run.cpp` becomes `src/tools/zeus_run/`: command line, launch, application mode, dialogs, language packs, diagnostics; no change of behaviour or options.
- **Contact sheet of screenshots.** `tools/contact_sheet.py OUT.bmp COLUMNS SHOT.bmp …` puts several captures, halved, in one grid, to review a scripted run at a glance (standard library only).
- **Scenarios in parallel.** `tools/check_scenarios.py --jobs N` (default: cores minus one) plays the eight scenarios at the same time, each in a fresh write directory, and prints the results in alphabetical order as before: 2 min 10 s instead of about 8 to 20 min depending on the load. `tools/publish.py merge` now stops at once on a pull request in conflict with `main`, or whose branch was not pushed.
- **Counting passes and measuring the game speed without editing code.** `zeus-run --count ADDRESS[,…]` prints how many times execution reached each game address, natively rewritten functions included; `--ticks` prints the simulation ticks per second in real time, pauses left out. Code hooks on the same address now chain instead of replacing each other.
- **Unit tests.** A new `zeus-tests` executable, run by the automated build, tests the host parts that do not need the game: WAV reader, test-script parser, Windows-1252 ↔ UTF-8, case-insensitive file lookup, and the sprite dispatcher's clipping (now in `sprite_clip.h`).
- **Shorter source files.** `kernel32.cpp` is split into environment and time, memory and character sets, and files; no change of behaviour.
- **One command to merge or package a release.** `tools/publish.py merge N` waits for the pull request's checks, fast-forwards `main` locally, checks that the history has a single identity, pushes and deletes the branch; `tools/publish.py release X.Y` builds the application, verifies its signature and self-test, zips it and searches every file of the archive for words that must never be published, then prints the SHA-256.
- **Every game address in one place.** The addresses of the GOG executable the host reads, writes or intercepts are named and commented in `src/win32/game_gog.h`, and checked for their expected bytes by one function; behaviour unchanged.
- **Text file format documented.** `formats/eng.md` and `tools/engtext.py` describe `Zeus_Text.eng`, `Zeus_Editor_Text.eng` and `Zeus_MM.eng` (header, string groups, help records, `@` formatting codes, character coding), with evidence from the game's loader and lookup code; the reader parses and re-serializes all 14 text files of the editions we have, byte for byte.
- **MP3 decoding through FFmpeg instead of AudioToolbox.** The 929 MP3 files give the same rate, channels and frame count as before (738,188,928 frames, 16,738.978 s); the reduced LGPL FFmpeg gains the MP3 decoder. AudioToolbox is no longer linked.
- New screenshots on the front page: a large city in wide mode (1324 × 768) and the main menu, both with a French language pack.
- **A clearer front page.** The README, in English and in French, opens with three steps to play, what you get, the next steps as task lists linked to their issues, and the history of every version since 0.1. The roadmap splits each step into precise tasks (#34 to #42).

## 1.0.1 — 2026-10-14

The game now runs at its real speed (it ran at two thirds of it since 0.2), with less CPU.

- **The game runs at its real speed.** A simulation tick happens only when a turn of the game's
  loop finds its interval elapsed (22 ms at the default 90 % speed), and the host's fixed 16 ms
  pacing rounded that up to 33 ms: since 0.2 the game ran at two thirds of its speed. The host
  now wakes up when the next tick is due: 45 ticks a second at 90 % instead of 30, on 42 % of a
  core instead of 52 %. Scenarios are unaffected (they run on a virtual clock).
- **Native sprite dispatcher.** The function every sprite goes through runs natively for its two
  common cases (run-length and opaque images, neither clipped sideways nor mirrored, 95 % of
  the calls); every other case falls back to the original code. A large city needs 42 % less
  computing time; frames are identical.
- **A new roadmap.** Four horizons after 1.0 — 1.1 *Cool and quiet*, 1.2 *Every edition*, 2.0
  *Everywhere* (Linux, Intel Macs), 3.0 *Recompiled* (no emulator, the player's copy translated
  on their own machine) — and an exploration of the other City Building Series games. Each step
  has a milestone and an issue on GitHub; the roadmap also looks back on what 1.0 taught.

## 1.0 — 2026-10-14

First stable version. The code is that of 0.9, which has now been played by hand: adventures
of Zeus and Poseidon, 3D sound by ear, the pause when switching to another application with
⌘Tab, the mouse wheel, the French pack built by the application from the original CDs. Nothing
had to be fixed.

## 0.9 — 2026-10-14

Release candidate for 1.0, meant to be played and reported on before it gets that number.

- **Lighter.** The host keeps in memory only the game files of 4 MB or less, which the game
  reopens all the time; its large picture files are read once per game and the game keeps them
  itself. A running large city uses about 530 MB instead of 700. Memory stays flat over a
  20-minute session.
- In wide mode the display options read the real size ("1324 x 768 resolution", or
  "Résolution 1324 x 768" in French), as does the help for the F7 key. The game's text files
  are left alone: the host replaces the label in the copy it hands to the game.
- **Starts in under a second.** At launch the game reads its event messages and, for each of
  them, looked up its name in a table of 4,172 entries one comparison at a time: 18 million
  calls, three to four seconds of black window. The host now does that look-up natively. The
  table it produces is identical, in English and in French.
- Documentation brought up to date: what works, what runs natively, what is left.

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
