# Tests

There is no game file in this repository, so the tests that exercise the game need your own
copy (see [docs/getting-started.md](../docs/getting-started.md)).

## Without the game

```bash
./build/zeus-run --selftest
./build/zeus-tests
```

The first checks the emulated processor and the call mechanism. The second
(`src/tests/zeus_tests.cpp`) tests the parts of the host that do not need the game, on cases
built in memory: the WAV reader, the test-script parser, Windows-1252 ↔ UTF-8 conversion, the
case-insensitive file lookup of the virtual file system (in a temporary folder under `build/`),
and the sprite dispatcher's clipping. The automated build runs both on every change.

## With the game: scenarios

A scenario is a script of mouse and keyboard actions (`tests/scenarios/*.txt`). Played with a
virtual clock, it always produces the same frames, so a change to the host can be proven not to
alter the game.

```bash
python3 tools/check_scenarios.py record     # before your change: store the reference logs
python3 tools/check_scenarios.py            # after: replay and compare, frame by frame and tick by tick
python3 tools/check_scenarios.py --no-native   # same, with every game function emulated
python3 tools/check_scenarios.py --only tutorial --verify 0x0050B410   # also check one function call by call
```

`record` plays the scenarios with every game function emulated (`--no-native`): the references
are those of the original code. Each run writes two logs, the fingerprint of every displayed
frame and the fingerprint of the game state at every simulation tick
([docs/equivalence.md](../docs/equivalence.md)); both must match the reference. Reference logs
are kept in `build/scenarios/`; they depend on the game files and are not versioned. A scenario
also fails if the game stops on a missing function.

| Scenario | What it plays |
|---|---|
| `tutorial` | profile, first tutorial, a house, keyboard scrolling |
| `adventure` | first Zeus adventure, every tab of the control panel, building attempts |
| `wide` | wide mode: mode switch, information window, panel at its new position |
| `poseidon` | first Poseidon adventure, a road drawn by dragging, housing, world map and back |
| `editor` | adventure editor: adventure list, editor screen, city map, terrain brush, menus |
| `options` | *File* menu, every window of the *Options* menu, in-game help, *About* |
| `window` | window sent to the background and back (the game waits), mouse wheel in a list |
| `working-city` | the last tutorial's large city: sanctuaries, stadium, a god and a monster |

Scenarios were written for the English GOG edition at 800×600. Lines starting with `#` are
comments; `# durée : N` gives the length of the run in virtual seconds and `# options : …` extra
options for `zeus-run`.

### On a fresh Linux machine, in one command

```bash
python3 tools/bench_setup.py --apt --game /path/to/game/app -- --references /path/to/shared/references
```

`tools/bench_setup.py` installs the build packages (`--apt`), links `original/gog/app` to the game
folder, builds the project, then runs `check_scenarios.py` with what follows `--`. Ubuntu 24.04
ships Unicorn 2.0.1, which cannot use non-executable pages: every memory write then takes the
emulator's slow path and a scenario runs about four times slower. When `pkg-config` finds no
Unicorn 2.1 or later, the script builds Unicorn 2.1.4 from its source (x86 only, about twenty
seconds) in `build/deps/unicorn` and builds the project against it.

`--references DIR` keeps the references in a folder shared between machines instead of
`build/scenarios/`, and records those that are missing before comparing. They are filed under
`DIR/<key>/`: the key sums up what a run of the pure original depends on (the game executable,
the host code except the rewritten functions in `src/win32/native*`, their lines in
`CMakeLists.txt` and their addresses in `src/win32/game_gog.h`, the Unicorn and FFmpeg versions, the system), described in
`DIR/<key>/cle.txt`. A branch that only changes native functions finds the references of `main`;
a change to the rest of the host records new ones once. `record --references DIR` records them
again. The references are derived from the game: keep them out of the repository and out of
anything published.
