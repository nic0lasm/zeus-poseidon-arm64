# Ukrainian: an example of a language pack for another alphabet

*Zeus + Poseidon* never came out in Ukrainian. A Ukrainian pack was made for this project from
the English GOG edition, with the tools of `tools/`: this folder holds what that pack needs and
what anyone may share, as an example for any other language, and especially for one written in
another alphabet.

| File | What it is |
|---|---|
| `alphabet.txt` | The settings of the pack: code page, alphabetical order, the new letters, the letters that share a Latin glyph, a few special entries of the character table. |
| `skeletons.txt` | The new letters, drawn one pixel wide, in three sizes. They were drawn for this project: they are not game data. |

What is **not** here: the translation itself and the built pack. The PO files hold the game's
English text next to the translation, and a pack is made of the game's files rebuilt: like every
pack ([docs/languages.md](../../../../docs/languages.md)), they stay on the translator's computer
and are not shared.

## Making a pack

From your own copy of the game (`GAME` is its folder):

```bash
python3 tools/translate.py export GAME po/ --language uk     # one PO file per text set
# translate the PO files with any PO editor (Poedit, Lokalize, a text editor)
python3 tools/alphabet.py build GAME tools/examples/lang/uk pack/    # fonts with the new letters
python3 tools/translate.py build GAME po/ pack/ --lang-dir tools/examples/lang/uk
./build/zeus-run --run GAME/Zeus.exe --language pack/
```

In the application, copy `pack/` to `~/Library/Application Support/Zeus Poseidon/Packs/` under
the name the language list should show, and hold Option (⌥) at launch to pick it.

`translate.py export` can be run again later, after a new version of the game or of the tool: the
translations are kept, and an entry whose English text changed is marked fuzzy. `translate.py
build` checks every string (variables such as `[city_name]`, `%s`, the help's `@` codes, the code
page, the glyphs of the character table) and stops on an error. `alphabet.py sheet` draws the new
letters of every font into a PNG, to look at them before playing.

A language written in the Latin alphabet may not need `alphabet.py` at all: the fonts already
have the accented letters of Western and Central Europe, and the character table of the
executable of that language's edition, if there was one, is described in docs/languages.md.

## How the Ukrainian pack was made

**Texts.** Five sets: the interface (`Zeus_Text.eng`, about 8,250 strings), the editor's few
strings of its own, the in-game help (`Zeus_MM.eng`, 1,751 titles, captions and pages), the event
messages (3,996) and the titles and texts of the adventures (114). By words, about 96 % of the
interface, 98 % of the messages, more than 99 % of the adventures and 62 % of the help are
translated: the 161 reference pages of the help (records 0–160) were left in English by choice.
The help's table of contents is rebuilt in Ukrainian alphabetical order (`order` in
`alphabet.txt`).

**Code page.** Windows-1251 (`code_page: 1251`). The pack's `codage.txt` makes the host convert
typed text, such as a leader's name, to it, and the names of the files the game writes.

**Letters.** The game has 17 fonts in `Zeus_Fonts.sg3` / `zeus_Fonts.555`, 134 glyphs each; their
small letters are small capitals. Ukrainian needs 33 letters in two cases, plus the guillemets
« » and the em dash. Eleven letters look like Latin ones in these fonts (А В К М Н О Р С Т Х І),
so they and their small capitals use the Latin glyphs (`same_as_latin`). The 22 others, in both
cases, and the three signs (`new_upper`, `signs`) are new: 47 glyphs per font. They take slots
81 to 134, where the fonts keep the accented Western letters that Ukrainian text never uses (slot
130, the degree sign, stays). Е is drawn anew: the Latin E of these fonts looks too much like Є.

**Drawing the glyphs.** Most fonts draw a letter as a dark stroke one pixel wide plus a lighter
copy of it shifted by (+1, +1); the 22 pt fonts double it, `SPR_FONTS` is a mask the game colours.
`alphabet.py` lays out each skeleton the same way, then gives every pixel the colour of the pixel
of the same font's Latin letters whose 5 × 5 neighbourhood is most alike (counting whether it is
dark or light and how high it is above the baseline). The texture and the colour gradient of each
font are kept, from your own copy of the game, at build time. The steep strokes of Л, Д and У get
a second pixel per row, as the Latin A and V have.

**Placing the glyphs.** The game raises a glyph by its height minus the line height, which suits
the accented Latin letters it was made for, not letters going below the line (Д, Ц, Щ) or with a
mark above (Й, Ї, Ґ). The pack's `remontee.txt` (`ancre`) makes the host read each glyph's lift
from the anchor y of its image instead, which `alphabet.py` sets from the skeleton's baseline.

**Writing the fonts back.** Two rules learnt the hard way: the pixel data of the images must stay
contiguous, in record order (the game does not take data appended at the end of the `.555`), and
each row of a compressed image is encoded on its own, with runs of at most 16 colours.

**Typography.** The fonts have one apostrophe and no curly quotes, ellipsis or no-break space:
`translate.py` turns ’ into ', … into ..., a no-break space into the game's `_`; the character
table sends the curly quotes of Windows-1251 to the straight ones and its dash to the em dash
(`table` in `alphabet.txt`).

**Not done.** The names of the adventures in the list are their file names, still in English. The
reference pages of the help are in English.
