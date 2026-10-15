# Changelog

Notable changes, most recent first. Each entry of the "unreleased" section corresponds to a
pull request.

## Unreleased

- **Second pass of the city drawing in native code.** What stands on each tile (`0x00458170`, from `0x00458792`): ground drawn again, ambient counters, building tops and sloped tops, `0x0044F320` and the animation step when they draw nothing, and the walkers' ring when the lists are empty, through a host jump that hands back to the original mid-cell, with its registers, at the first step it does not handle. The native ground tile `0x00457900` now also draws sloped pieces, tiles with flags `0x4020` and tiles with a building. In the large city of the last tutorial 81 % of the cells of this pass stay in native code; in real time its CPU time per simulation tick falls by about a third, and a small city goes from 37 % to 23 % of a core. Part of [#71](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/71).
- **Building tops and clipped run-length images in native code.** `0x004163B0` and `0x004165D0` (a run-length image clipped on the left or on the right) are rewritten whole; the sprite dispatcher's fast path uses them, and `0x005ADF00` (the top of a building, 937 calls per city frame) gets a fast path for every case but the overlay colour. The hidden-row skip `0x004170B0` now keeps the original's partial-register quirk (a run of 128 pixels or more inflates the next count on the same row by 256). Part of [#71](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/71).
- **Equivalence harness.** `zeus-run --state-log` writes a fingerprint of the game state at every simulation tick (the 136 variables the game writes into a saved game, located from a save made by the GOG executable, plus the random generator's tick area and the variables recomputed on load); `tools/check_scenarios.py` now records its references with `--no-native` and compares the state tick by tick as well as the frames. `zeus-run --verify ADDRESS[:END]` runs sampled calls of a game function twice from the same state, native replacements off then on (`Cpu::set_native`), and compares registers and memory, stopping at the first difference. Over the nine scenarios the state is identical at all 13,073 ticks and every native replacement passes (268,312 calls compared); see `docs/equivalence.md` ([#26](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/26)).
- **Interface and texts documented.** `docs/engine/interface.md`: mouse and keyboard from the window procedure to the per-turn edges, the screen switch, the 28- and 40-byte widget tables, the control panel, information, message and tooltip windows, fonts, the glyph table, the word and line layout with its `@` codes, call counts on three scenarios and the ordered list of 22 steps to rewrite; 71 functions named in the code map. Part of [#71](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/71).
- **Terrain documented.** `docs/engine/terrain.md` describes the map grids of `Zeus.exe` for a hand-written rewrite: the 228 × 228 grids (terrain flags, altitude, ground images, regrowth, desirability, tree depth, meadows…) with their addresses and save and map blocks, the terrain bits the code confirms, the 51-step cycle that runs the rules, desirability spreading (model values, square and rectangular rings, ±100 clamp), the regrowth of trees, reeds and meadows with grazing and timber mills, and the test that decides whether an area can be built on; every claim carries its address and status, and the page ends with the ordered list of functions to rewrite and their call counts. `analysis/symbols.tsv` names and marks 38 more functions as documented. Part of [#71](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/71).
- **Honest engine counters.** The native share is now measured on `working-city` over the game's own x86 code only (host work excluded, reported apart) and given as time and as code bytes: 54 % (+ 12 % partly native); `tools/coverage.py` computes both. All counters are out of the GOG executable's 4,872 functions (8,397 was the 2.1 edition) (#69).
- **Published adventures (`.pak`) documented.** `formats/pak.md`: a `.set` header followed by copies of the adventure's map files; the editor writes the header twice (`0x0042AD86`) and copies each map through a 500,000-byte field of the adventure object, which explains stale map offsets and the map left in that field. `tools/pakfile.py` reads the 130 `.pak` files of the game (259 maps) and writes them back identical. Part of [#30](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/30).
- **Adventure files (`.set`) documented.** `formats/set.md` describes the 54 fields the editor writes (`0x00425910`), how the reader adapts versions 5 to 26, and what is known of each field (episode texts and scenarios, events, embedded parent-city map, adventure kind); `tools/setfile.py` reads the 28 `.set` files of the game and writes them back identical. Part of [#30](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/30).
- **Maps rewritten byte for byte.** `tools/mapfile.py rewrite` reads the 57 maps of the installations (versions 247 to 321) and writes them back identical, using the save-game compressor; `formats/map.md` gains the map writer and reader of the GOG executable and the meaning of the last field of a table entry. Part of [#30](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/30).
- **Saved games: grids.** `formats/sav.md` ties more of the 228 × 228 grids to the code: building and figure per tile (with the per-tile figure chain), desirability, resource regrowth, grids rebuilt from the view orientation, the variables the game recomputes on load, and value counts for every grid of a large city. Part of [#70](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/70).
- **Saved games read and rewritten byte for byte.** `tools/savefile.py` reads every saved game (versions 235 to 321; 219 files checked) and writes it back identical, including the compressed blocks: its hand-written PKWARE DCL implode reproduces the game's output exactly once it keeps one work structure for the whole file, as the game does (at the end of a stream, implode compares bytes past the data). `samples` makes the test saves with `zeus-run --deterministic` (empty city, tutorial, road, large city), which come out identical from one run to the next. `formats/sav.md` gains the building and figure records as created by the game (`0x00493E70`, `0x004D9FD0`), the random generator, the message list, treasury and population. Part of [#70](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/70).
- **Saved games: block table.** `formats/sav.md` lists the 139 blocks written by the GOG executable (`0x0050E2C0`), each with its address in memory, shape and compression, what is known of its content, and how the reader (`0x0050CB80`) adapts the 138-block files of older versions. Saves use PKWARE DCL implode (zlib is not involved). First step of [#70](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/70).
- **Rendering documented.** `docs/engine/rendering.md` describes the rendering of `Zeus.exe` for a hand-written rewrite: surfaces and presentation, sprite banks, the sprite dispatcher (`0x005ACF50`) and its routine for every image type, clipping, overlays and colour mixing, the isometric projection and the three passes of the city drawing loop (`0x00458170`), with addresses, status (verified or hypothesis) and call counts; it ends with the ordered list of functions left to rewrite.
- **Code map and coverage counters.** `analysis/symbols.tsv` lists every function of the GOG executable (address, size, name, module, status, evidence; no code), built by a read-only Ghidra script and `tools/build_symbols.py` from `analysis/noms.tsv` (the hand-given names, which replace `analysis/notes/symbols.tsv`); `tools/coverage.py` counts functions per status and module and the share of executed code that runs natively; `zeus-run --profile` now also writes the size of each block; `docs/engine/README.md` and the README show the counters (#69).
- **New plan.** The roadmap now leads to a hand-written, documented native engine proven against the emulated original: instruments, data, engine by subsystem, then 2.0 *Native engine*; modding later on a branch. The 3.0 *Recompiled* path is dropped. Roadmap, README and README.fr.md rewritten.
- **Roadmap after 1.1.** New milestone 1.2 *Lighter* (equivalence harness, then the second drawing pass and the simulation, toward a city under 20 % of a core); languages become 1.3; #35 and #36 concluded without merging. Roadmap, README and README.fr.md aligned.

## 1.1 — 2026-10-14

- **Native ground drawing in the city.** The first pass of the city drawing loop (`0x00458170`), the ground of each tile (`0x00457900`) and the 58 × 30 diamond tile (`0x00435070`) run on the host for their common cases, through a new host jump (`Cpu::add_code_jump`) that hands back to the original code with the same registers and variables for every other case. A small city at the default speed went from 51–60 % to 35 % of a core with the same game speed; the large city of the last tutorial barely changes (85 % to 84 %), its cost being in the second pass and the simulation. The eight scenarios give identical frames.
- **Taller wide modes.** Above 768 lines the control panel is extended with its own texture instead of a black band, the building information window opens at the bottom of the screen, and the world map no longer reads past its image (a band of noise under it). `--wide auto` now adds lines on screens taller than 16:9 (1366 / aspect, up to 1023: 16:10 → 1324×843). New `tall` scenario at 1444×903 ([#23](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/23)).
- **Event messages documented.** `formats/eventmsg.md` describes `Zeus eventmsg.txt` (4,172 phrases looked up by name in a table of the executable, how the game skips unknown words and stops at an inner quote) and `Zeus event message categories.txt`; `tools/eventmsg.py` reads all 11 files of the game and, given the executable, checks every phrase name.
- **Building plans documented.** `formats/layout.md` describes the sanctuary, pyramid and palace plans of `Model/` (piece records, the two lists, how the game's `sscanf` copes with damaged lines); `tools/layout.py` reads all 102 files of the game.
- **First launch with fewer clicks.** The file picker opens in Downloads (no folder-access prompt); a window says the game is being unpacked (about a minute) and closes by itself; with no language pack, the game starts in English without asking (Option still opens the list); dropping the GOG installer on the application icon unpacks it straight away (the bundle now declares `.exe` documents without becoming their default app). A player now does 11 actions instead of 14 ([#55](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/55)).
- **`Model/*.txt` documented.** `formats/model.md` describes the difficulty tables (buildings and houses, figures and enemies, tax sentiment, monster strength) and how the game reads them; `tools/model.py` reads all 63 files of the game and checks them against a re-implementation of the game's own parser.
- **Linux build in CI.** The automated build gains an `ubuntu-latest` job (Unicorn, SDL2 and FFmpeg from apt; build with warnings as errors, `zeus-tests`, `--selftest`). Off macOS, CMake no longer links the Apple frameworks and JPEG backgrounds are decoded by FFmpeg (a few pixels differ from ImageIO, see #38). With Unicorn older than 2.1 (Ubuntu 24.04 ships 2.0.1) the non-executable-page protection is skipped, which is slower but correct. The AppImage and the arm64 job wait for later steps. Part of #25.
- **Download in one click, honest first-launch steps.** The README (English and French) opens with a direct link to `releases/latest/download/Zeus-Poseidon-macos-arm64.zip` (`tools/publish.py release` now also writes the archive under that version-free name, same bytes), gives the exact macOS 15 steps (*Privacy & Security → Open Anyway*; right-click → *Open* no longer works), says where to get the GOG installer from a Mac, and that an Apple Silicon Mac is required ([#55](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/55)).
- **Portable first-launch dialogs.** Application mode now asks its questions through a small interface (`dialogs_macos.cpp` unchanged in behaviour: AppleScript and the Option key; `dialogs_sdl.cpp` elsewhere: SDL message boxes, zenity/kdialog file pickers if installed, command-line questions otherwise). On other systems the support folder is `$XDG_DATA_HOME/Zeus Poseidon`, and `ZEUS_ASK_LANGUAGE=1` asks the language again. Part of #25.
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
