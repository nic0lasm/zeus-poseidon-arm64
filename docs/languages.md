# Other languages

The program runs the executable of the GOG edition, which is in English. To play in another
language you build a **language pack** from your own copy of the game in that language, for
instance the original French CDs. The pack stays on your Mac; it is made of game files and must
not be shared.

Status: French works (menus, tutorials, missions, accented text, typing accented names).
Other Western European editions should work the same way but have not been tried.

## What a pack contains

A pack is a folder whose files take precedence over those of the GOG installation:

- the text files (`Zeus_Text.eng`, `Zeus_MM.eng`, …) and the event messages;
- the fonts;
- the `Adventures` folder, taken whole because its files have translated names;
- voices and videos;
- `table-caracteres.bin`, the 224-byte character table of the executable of that language;
- `remplace.txt`, the list of folders the pack replaces entirely.

## Building a pack from the original CDs

You need the images of both CDs (*Zeus* and *Poseidon*) in the language you want and, if you
have them, the official 1.1 and 2.1 updates in that language. Put them in one folder; the files
are recognised by their content, whatever their names.

**With the application** — hold the Option key (⌥) while it starts, or use the dialog shown on
first launch: pick *Another language, from the original CDs…*, then the folder, then a name.
The pack is built in about a minute and appears in the list. Each language keeps its own saved
games. Packs live in `~/Library/Application Support/Zeus Poseidon/Packs/`.

**From the command line**

```bash
./build/zeus-run --make-pack FOLDER original/gog/app packs/fr
./build/zeus-run --run original/gog/app/Zeus.exe --language packs/fr
```

Either way nothing from the CDs is executed: the images are read, the installation archives
unpacked and the updates applied by the Python tools of this project (`tools/`), which the
application carries along with its own Python interpreter. About 3 GB of free space is needed
while the pack is being built. Without the 2.1 update the pack takes the files of the CDs,
which may not match the 2.1 game exactly.

The steps can also be run one by one: `tools/cdimage.py`, `tools/ishield.py`, `tools/wise.py`,
`tools/build_install.py`, then `tools/make_language_pack.py`.

The executable of the other edition is only read, to copy its character table. It is never
run, and its copy protection is left alone.

## Why files are not enough

The GOG executable is a Central European build: its character table follows Windows-1250 and
the vertical placement of accented letters is tuned for Polish. With French text files alone,
`à` shows the wrong glyph and accented letters float above the line. The host therefore
adjusts three things in memory when a pack is loaded, without touching any file; details are in
[reverse-engineering.md](reverse-engineering.md).
