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

To check the sounds of a pack, `./build/zeus-run --check-audio packs/fr` decodes every file the
way the game's mixer will. One oddity of the French CDs shows up there:
`Audio/Voice/Walker/trd_e_1.mp3` is an empty file on both discs, so that one line of the trader
is silent, as it was in the original French edition.

The executable of the other edition is only read, to copy its character table. It is never
run, and its copy protection is left alone.

## Why files are not enough

The GOG executable is a Central European build: its character table follows Windows-1250 and
the vertical placement of accented letters is tuned for Polish. With French text files alone,
`à` shows the wrong glyph and accented letters float above the line. The host therefore
adjusts three things in memory when a pack is loaded, without touching any file; details are in
[reverse-engineering.md](reverse-engineering.md).

## Translating into a new language

A language that never had an edition of its own can be added from the English texts: see
[tools/examples/lang/uk](../tools/examples/lang/uk/README.md), the Ukrainian pack made that way.
`tools/translate.py` writes the game's texts into PO files and builds a pack's text files from
the translated ones; `tools/alphabet.py` draws new letters into the fonts from one-pixel skeletons
and writes the character table. The PO files and the pack hold the game's text: like any pack
they stay on the translator's computer.

## Packs for another alphabet

A pack may also bring its own fonts, for a language whose letters the original fonts do not have
(Cyrillic, for instance: new glyphs drawn in place of the accented Western letters of
`Zeus_Fonts.sg3`, with a character table that maps the code page's letters to them). Two optional
files then tell the host how to treat it:

- `codage.txt`: the code page of the pack's texts, `1251` for Cyrillic (default `1252`). Typed
  text (a leader's name) is converted to it, and so are the names of the files the game writes.
- `remontee.txt` containing `ancre`: the vertical lift of each glyph of the characters `0x80` to
  `0xFF` is read from its image (the anchor y field, `+0x24`, zero in the original fonts) instead
  of being worked out from the image height. Letters with a descender (Д, Ц, Щ) can then go below
  the line, and a mark above a letter (Й, Ї) sits at the same place in both text routines
  (`0x00508C84` and `0x004F5AD4`, where the computed lift is in ECX).

Two things to know when rebuilding `Zeus_Fonts.sg3` / `.555`: the pixel data of the images must
stay contiguous, in record order (the game does not tolerate data appended at the end), and each
row of a compressed image is encoded on its own, with runs of at most 16 pixels.
