# Fonts — `Zeus_Fonts.sg3` + `zeus_Fonts.555`

Every font of both games is in one sprite file, `DATA/Zeus_Fonts.sg3` (version `0xD5`), with its
pixels in `DATA/zeus_Fonts.555`; *Poseidon* adds no font file. The container itself (header,
records, run-length encoding) is described in [sg3.md](sg3.md); this page covers what is specific
to fonts, and the character table of the executable that maps text to glyphs. How the game lays
out and draws text is in [docs/engine/interface.md](../docs/engine/interface.md), sections 7.2
to 7.4. Reader: `tools/fonts.py` (standard library only, on top of `tools/sg3.py`).

State on 10 November 2026, on the GOG build (`Zeus.exe` SHA-256 `382bcd28…75e90a57`, the same file in
the Steam edition): `fonts.py check` decodes all 2,612 images and confirms every **verified** layout
statement below.

## What the file holds

| Images | Bitmap | Content |
|---|---|---|
| 1–200 | `Zeus_system.bmp` | not a font of the group table: 160 one-colour masks (type 20) of 1–17 × 9–14 pixels and 31 other images; 192–200 are empty records. Role not established. |
| 201–2612 | `Zeus_Fonts.BMP` | 18 blocks of 134 glyph images, one block per font |

| First image | Group name | Rank | Kind | Height (most common) | Width |
|---|---|---|---|---|---|
| 201 | `SPR_FONTS` | 0 | mask | 8–15 (10) | 2–16 |
| 335 | `SPR_FONTS_11C` | 1 | colour | 14–20 (15) | 2–22 |
| 469 | `SPR_FONTS_11C2` | 3 | colour | 14–20 (15) | 3–22 |
| 603 | `SPR_FONTS_11C3` | 4 | colour | 14–20 (15) | 2–22 |
| 737 | `SPR_FONTS_11C4` | 2 | colour | 14–20 (15) | 3–22 |
| 871 | `SPR_FONTS_22C` | 6 | colour | 29–36 (29) | 4–43 |
| 1005 | `SPR_FONTS_22C2` | 7 | colour | 29–37 (29) | 4–43 |
| 1139 | `SPR_FONTS_9B` | 8 | colour | 11–15 (11) | 2–16 |
| 1273 | `SPR_FONTS_11CB` | 5 | colour | 14–20 (15) | 3–22 |
| 1407 | — | — | mask | 10–15 (10) | 2–16 |
| 1541 | `SPR_FONTS_11LTYEL` | 9 | colour | 14–20 (15) | 2–22 |
| 1675 | `SPR_FONTS_9LTBK` | 10 | colour | 11–15 (11) | 2–16 |
| 1809 | `SPR_FONTS_9LTBU` | 11 | colour | 11–15 (11) | 2–16 |
| 1943 | `SPR_FONTS_9LTYEL` | 12 | colour | 11–15 (11) | 2–16 |
| 2077 | `SPR_FONTS_9DKBU` | 13 | colour | 11–15 (11) | 2–16 |
| 2211 | `SPR_FONTS_9DKYEL` | 14 | colour | 11–15 (11) | 2–16 |
| 2345 | `SPR_FONTS_11LTBLUE` | 15 | colour | 14–20 (15) | 2–22 |
| 2479 | `SPR_FONTS_DKMESSAGE` | 16 | colour | 9–13 (12) | 2–13 |

- The **rank** is the order of the group names, counting only non-empty groups ([sg3.md](sg3.md),
  "Rang des groupes"); it is the order the game uses (interface.md 7.2, table `0x012AFBEC`). The
  blocks themselves are not stored in that order. **verified**
- The other 282 group names of the file (`SPR_B_PALACE03`… `SPR_TAG299`) have no image: the name table
  is shared with other sprite files. **verified**
- **Block 1407** has no group name, so the group table never reaches it: it is a second set of
  masks, slightly taller than `SPR_FONTS`. Whether some code reaches it by image number is not
  established.
- **Records.** In all 2,412 glyph records the anchor fields (offsets 34 and 36) are 0, there is no
  mirror reference (offset 16) and no external pixels (offset 52). The masks are type 20, every other
  glyph type 0. All glyphs are run-length encoded (offset 51 = 1) except 51 stored plain, all 2 to 5
  pixels wide: mostly `!`, `¡`, `i`, `I`, `Í` and `Ì`. **verified**
- The game passes a font around as the image identifier of its first glyph; glyph `g` of a font is
  the image `first + g − 1` (interface.md 7.3). **verified**

## Glyph numbers

The 134 slots hold the same characters in every font. **verified** (the 22C, 11C, 9B and DKMESSAGE
sheets of `fonts.py sheet`, compared by eye; the order of 1–80 also follows from the character table).

| Glyphs | Characters |
|---|---|
| 1–26 | `a`–`z` (in the fonts drawn in small capitals, such as `SPR_FONTS_22C`, these are small capitals) |
| 27–52 | `A`–`Z` |
| 53–62 | `1`–`9`, then `0` |
| 63–80 | `! " % * ( ) - + = : ; ' ? \ / , . _` |
| 81–100 | `ä á â à`, `ë é ê è`, `ï í î ì`, `ö ó ô ò`, `ü ú û ù` |
| 101–111 | `ç ñ æ ß Ä É Ü Ñ Æ Œ œ` |
| 112–126 | `Á Â À`, then an `E` with an acute accent placed higher than 106's, `Ê`, `Í Î Ì`, `Ó Ô Ò Ö`, `Ú Û Ù` |
| 127–134 | `¡ ¿`, a lone circumflex-like mark, `°`, `Å Ø å ø` |

Glyphs 81–134 were read from the pictures of `SPR_FONTS_22C`, where the accents are largest; the
four-accent order of 81–100 is plain, but the identity of 106 and 115 (both an `E` with an acute
accent) and of 129 is a **hypothesis**. Note that space, `#`, `$`, `&`, `@` and the brackets have
no glyph (interface.md 7.3).

## The character table

The executable maps each character code from `0x20` to `0xFF` to a glyph number, one byte per
character, 0 for none: 224 bytes at `0x0063BE2C`, in `.data`; in the GOG `Zeus.exe` that is file
offset `0x23BE2C` (`fonts.py table`). **verified**

- It is the table of interface.md 7.3 seen from code `0x20` (the 256-byte view starts at
  `0x0063BE0C`, whose first 32 bytes are not glyph numbers).
- The GOG build is a Central European one: its table maps Windows-1250. 185 characters point at 130
  of the 134 glyphs; glyphs 110, 111, 128 and 129 are not used. Several characters share the
  nearest picture, so some Central European letters are drawn with a Western one. **verified**
  (`fonts.py table`).
- **In another edition's executable** the table is found by its content: `A`…`Z` (codes `0x41`–`0x5A`)
  map to 27…52 and `a`…`z` to 1…26. A language pack copies the 224 bytes of its edition into
  `table-caracteres.bin`, and the host writes them over the table at load time
  (`src/win32/language.cpp`, [docs/languages.md](../docs/languages.md)).

## How the game uses a glyph

From interface.md 7.2–7.4 and [reverse-engineering.md](../docs/reverse-engineering.md) ("Text rendering
and languages"), the fields that matter:

- **width** (offset 20): the advance is the width plus one pixel (`0x00507F20`); the space has no
  glyph and a fixed width per font.
- **height** (offset 22): for codes `0x80` and above, the glyph is raised by `height − line height`,
  never less than zero (`0x00508B50` for single-line text, `0x004F59B0` for panel text). This is
  why the accented capitals, the tallest images, sit on the same line as the others; the GOG build
  adds 2 pixels for a list of Polish letters.
- **type 20** glyphs are one-colour masks drawn in the colour at `0x00F1397C` (`0x00416C30`); the
  coloured fonts are drawn as stored, and text styles draw the glyph once more, in colour 0, one
  pixel off, for an outline or a shadow.
- The anchor fields are 0 in every glyph; whether the glyph routines read them at all is not
  established.

## Pixels

**Two layers.** Most coloured fonts look like a dark stroke under a lighter copy of itself, shifted
one pixel right and down (one pixel right in `SPR_FONTS_9B` and `SPR_FONTS_9LTBK`; two-pixel strokes
in the 22-point fonts). Measured over every glyph of a font, with the median brightness as the
threshold, the share of lighter pixels that have a darker pixel at that offset: 69–74 % at (+1, +1)
for the 11-point and 9-point fonts and DKMESSAGE, 65–66 % at (+1, 0) for 9B and 9LTBK, 52–58 % for
the 22-point fonts. An observation on the pictures, not a rule of the game.

**Runs.** The container lets runs cross the end of a row ([sg3.md](sg3.md)); the glyphs never do:
0 of their 93,229 runs. No colour run is longer than 16 pixels. **verified**. When glyphs were
rewritten for a language pack, glyphs whose runs crossed row ends came out wrong in the game, so a
glyph should be encoded row by row; that the glyph routines need it is a **hypothesis** drawn from
that one observation, and whether the 16-pixel limit matters is not established.

**Order of the data.** In `zeus_Fonts.555` the data of each image follows that of the previous
record without a gap, after 4 leading bytes (image 1 starts at offset 4). **verified**. A rewritten
file whose image data was not in record order showed wrong glyphs; a font file written back keeps
the data contiguous in record order and updates the offsets and the size fields 28 and 32 of the
header. That the game relies on the order is a **hypothesis** from that observation.

## Reader

```
python3 tools/fonts.py list  [GAME]               fonts, ranks, sizes
python3 tools/fonts.py check [GAME]               decode every image, check this page's layout
python3 tools/fonts.py table [GAME|EXE] [cp1250]  character -> glyph
python3 tools/fonts.py sheet FONT OUT.png [GAME]  one font, 10 glyphs to a row, numbered
python3 tools/fonts.py text  FONT TEXT OUT.png [GAME] [cp1250]
```

`GAME` defaults to `original/gog/app`. Pictures are made from game files: write them inside the
project (`build/`), never commit them.

## Still open

- The role of images 1–200 and of the unnamed block 1407.
- Glyphs 106, 115 and 129 (see above).
- Whether the anchor fields and the 16-pixel colour runs matter to the game.
