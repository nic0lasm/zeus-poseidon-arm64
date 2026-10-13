# Other languages

The program runs the executable of the GOG edition, which is in English. To play in another
language you build a **language pack** from your own copy of the game in that language, for
instance the original French CDs. The pack stays on your Mac; it is made of game files and must
not be shared.

Status: French works (menus, tutorials, missions, accented text, typing accented names).
Long play sessions still need testing. Other Western European editions should work the same
way but have not been tried.

## What a pack contains

A pack is a folder whose files take precedence over those of the GOG installation:

- the text files (`Zeus_Text.eng`, `Zeus_MM.eng`, …) and the event messages;
- the fonts;
- the `Adventures` folder, taken whole because its files have translated names;
- voices and videos;
- `table-caracteres.bin`, the 224-byte character table of the executable of that language;
- `remplace.txt`, the list of folders the pack replaces entirely.

## Building a French pack from the original CDs

```bash
# 1. Unpack the CDs and the official updates (no installer is run)
python3 tools/cdimage.py extract cd-zeus.iso original/cd-zeus
python3 tools/cdimage.py extract cd-poseidon.iso original/cd-poseidon
python3 tools/ishield.py extract original/cd-zeus/DATA1.CAB original/cab-zeus
python3 tools/ishield.py extract original/cd-poseidon/data1.cab original/cab-poseidon
python3 tools/wise.py extract update_1_1.exe original/patch-zeus-1.1
python3 tools/wise.py extract update_2_1.exe original/patch-poseidon-2.1
python3 tools/build_install.py            # rebuilds the 2.1 installation in original/install

# 2. Build the pack
python3 tools/make_language_pack.py original/install original/gog/app packs/fr \
    --cd original/cd-zeus --cd original/cd-poseidon

# 3. Play
./build/zeus-run --run original/gog/app/Zeus.exe --language packs/fr
```

With the application, put the pack in
`~/Library/Application Support/Zeus Poseidon/Packs/` (for instance `Packs/fr`). The next launch
asks which language to play in and remembers the answer; delete `langue.txt` in that folder to
be asked again. Each language keeps its own saved games.

The executable of the other edition is only read, to copy its character table. It is never
run, and its copy protection is left alone.

## Why files are not enough

The GOG executable is a Central European build: its character table follows Windows-1250 and
the vertical placement of accented letters is tuned for Polish. With French text files alone,
`à` shows the wrong glyph and accented letters float above the line. The host therefore
adjusts three things in memory when a pack is loaded, without touching any file; details are in
[reverse-engineering.md](reverse-engineering.md).
