# `.eng` — text files

Reference reader: `tools/engtext.py` (`info`, `check DIR`, `dump FILE GROUP`; standard library only).
Everything is little-endian. Offsets are in bytes.

Status as of 2026-10-14: **14 of 14 text files read to the last byte and re-serialized
identically** (GOG English 2.1, the English and French 2.1 installations, the Poseidon and Zeus
1.x cabinets, the editor archives, and one patch that ships a whole editor file). The same files
are read by the game through the code described under "Evidence". Three further files named
`.eng` in the update patches are binary patch fragments, not text files; the reader skips them.

Three files share the extension, in two layouts:

| File | Signature (16 bytes) | Layout | Content |
|---|---|---|---|
| `Zeus_Text.eng` | `Zeus textfile.` + 2 NUL | [text file](#text-files-zeus_texteng-zeus_editor_texteng) | interface strings, grouped |
| `Zeus_Editor_Text.eng` | same | same | strings of the map and adventure editor |
| `Zeus_MM.eng` | `Zeus MM file.` + 3 NUL | [MM file](#mm-file-zeus_mmeng) | the in-game help ("Mythology Manual"): one record per topic |

The game loads `Zeus_Text.eng` and `Zeus_MM.eng` at start-up (editor mode loads the editor file
in place of the first). A translation is made by replacing these files: the layout is the same
in every language (the French files are identical in structure to the English ones).

## Text files (`Zeus_Text.eng`, `Zeus_Editor_Text.eng`)

| Offset | Size | Type | Meaning | Status |
|---|---|---|---|---|
| 0x00 | 16 | bytes | signature `Zeus textfile.\0\0` | verified |
| 0x10 | 4 | u32 | number of table entries in use, i.e. highest group number + 1 (367 in the 2.1 files, 334 in the Zeus 1.x one) | verified |
| 0x14 | 4 | u32 | total number of strings = sum of all counts in the table | verified |
| 0x18 | 4 | u32 | unknown; close to the number of words of the text (41 839 against 42 355 counted in the GOG file), not equal | unknown |
| 0x1C | 8 000 | 1 000 × (u32 offset, u32 count) | the group table, **1-based**: entry 0 is always (0, 0); entries after the last group are zero | verified |
| 0x1F5C | rest | bytes | the text area | verified |

An entry gives the **offset of the group's first string, relative to the text area** (0x1F5C),
and its **number of strings**. A group is a list of NUL-terminated strings one after the other,
addressed by (group, index). Groups are laid out in increasing offset order. A group with
count 0 (6 in the 2.1 files: 74 and 248 to 252) keeps the offset of the group that follows it.
Group sizes range from 1 to 436 strings. Group numbers are the identifiers the game code passes
to its text lookup.

The text area is the groups back to back, ending with one or two NUL bytes. In some files a
group is followed by a few dead bytes (leftovers of a text that was edited out: 32 bytes between
groups 9 and 10 of the GOG `Zeus_Text.eng`, 39 in its editor file); they belong to no group and
are kept by the reader as gaps.

A string is plain bytes up to the NUL. An empty string is legal and counts as a string. The
game's own lookup (below) skips over empty strings and control bytes, so an empty string at the
end of a group is only ever reached by index.

The GOG `Zeus_Text.eng` and `Zeus_Editor_Text.eng` have the same size (262 936 bytes) and the
same table; they differ only in the strings of a few groups.

### Evidence

Loader `FUN_0050ada0` (GOG `Zeus.exe` 2.1, address 0x0050ADA0) opens the text file and issues
three reads: 0x1C bytes (the header) into `DAT_00f29e70`, 8 000 bytes (the table) into
`DAT_00f27f30`, then the rest of the file (`file size - 0x1F5C`, which confirms the text area
starts at 0x1C + 8 000) into a heap block `DAT_00f29f64`. The table sits immediately before the
header in memory.

Lookup `FUN_00507bf0` (73 callers, 0x00507BF0), called with (group, index):

```
p = text_area + table[group].offset
while index > 0:
    if *p == 0 and (p[-1] >= 0x20 or p[-1] == 0): index -= 1     # end of a string
    p++
while *p < 0x20: p++                                             # skip control bytes
return p
```

The count field is never read by the game; it is there for tools. The reader checks that the
game's lookup returns, for every non-empty string, the same bytes as a plain NUL split.

## MM file (`Zeus_MM.eng`)

| Offset | Size | Type | Meaning | Status |
|---|---|---|---|---|
| 0x00 | 16 | bytes | signature `Zeus MM file.\0\0\0` | verified |
| 0x10 | 4 | u32 | record capacity: 1 000 | verified |
| 0x14 | 4 | u32 | records in use: 980 in all files read; records after them are all zero | verified |
| 0x18 | 80 000 | 1 000 × 80 bytes | records (below) | verified |
| 0x13898 | rest | bytes | the text area | verified |

The game reads 0x13898 bytes (header and records, `FUN_0050ada0`) then the text area into
`DAT_00f29f68`. The text area starts with 16 NUL bytes, so that offset 0 means "no text"; every
other string is referenced by exactly one record field, and the strings tile the area with no
gap, in record order, up to one final NUL.

### Record (80 bytes, 20 × u32)

| Offset | Type | Meaning | Status |
|---|---|---|---|
| +0x00 | u16 | page layout: 3 for 697 records, 0 for 273, 2 for 9 (the page code branches on `== 2` and then reads the picture fields), 1 for 1 | verified that it is read and compared; meaning of the values hypothesis (`FUN_004f2d80`) |
| +0x02 | u16 | special page kind: 0 for 972 of 980 records in GOG, 1, 2, 5, 6, 7 or 8 for records 220 to 228; the page code compares it with 1, 2, 4 and 7 | verified that it is read and compared; meaning of each value unknown |
| +0x04 … +0x43 | u16 × 32 | geometry and picture offsets of the page (positions, sizes, in pixels or sixteenths of a pixel): the page drawing code reads them as `short` at +4 to +0x20 | hypothesis |
| +0x38, +0x3C | u32 | flag and offset of a file name; zero in all records | unknown |
| +0x44 | u32 | offset of the **title** | verified |
| +0x48 | u32 | offset of the **caption** (second text, word-wrapped with the pictures; 0 = none, only 39 records have one in GOG) | verified |
| +0x4C | u32 | offset of the **body** | verified |

Offsets are relative to the text area. `FUN_004f2d80` (the help page drawing code, 5 467 bytes)
reads the three fields as `DAT_00f13cf4`, `DAT_00f13cf8`, `DAT_00f13cfc` (`+0x44`, `+0x48`,
`+0x4C`) plus `DAT_00f29f68`, draws the title with `FUN_00508670`/`FUN_00508800`, wraps the
caption with `FUN_005090a0`, and gives the body to the formatting routine `FUN_004f5300`.

Record 0 is the "About" page, record 1 the table of contents (its body is a list of links to
every other topic), 2 and the following the topics. Records are addressed by their number: a
link `@63` points to record 63.

### Formatting codes

Text in the body (and in the table of contents) carries `@` codes. `FUN_004f5300` is the code
that interprets them, character by character:

| Code | Effect | Status |
|---|---|---|
| `@P` | end of paragraph: the line is broken and the next line is indented by 50 pixels | verified (`FUN_004f5300`) |
| `@L` | line break, no indent | verified (`FUN_004f5300`) |
| `@G` + digits | an inline picture: the digits (decimal picture number, `FUN_00507fc0`) select an image from a sprite bundle, drawn on its own line | verified (`FUN_004f5300`) |
| `@` + digits | a link to the record with that number; its label follows, in the table of contents up to the next `@`. Handled by the help window, not by `FUN_004f5300` (which copies it) | verified on the data (every target is a record whose title is the label); drawing code not traced |
| `&` | a space that never breaks, inside link labels | observed (450 uses); not traced |
| `@@`, `@M` | each occurs once, in plain running text; any `@` followed by another letter is an ordinary character | observed |

Counts in the GOG file: `@P` 1 024, `@L` 1 007, `@G` 96 (picture numbers 1 to 56), links 861.
Leading spaces of a line are dropped by the formatter. The French file has the same codes (in
particular the same `@P`, `@L`, `@G`).

Inside any string, the renderer `FUN_00508800` also treats `_` (0x5F) as a space that cannot be
broken and ignores bytes below 0x20; `#` (0x23) inserts a text prepared by the game (a number or
a name) at that point.

## Character coding

One byte per character. The renderer takes a byte, subtracts 0x20, and indexes the font's glyph
table (`PTR_FUN_0063be2c`, `FUN_00508800`): there is no code-page conversion in the game, the
bytes are glyph numbers. The English files are ASCII with a few bytes above 0x7F. The French
files use **Windows-1252** (é = 0xE9, è = 0xE8, œ = 0x9C, « » = 0xAB 0xBB), which the reader
uses to print. Windows-1250 (for the central European editions) could not be checked: no such
file was available. The glyphs themselves live in the sprite files (see `formats/sg3.md`), which
is why a language pack carries its own fonts.

## Files tested

`check` over `original/` and over the French language pack:

| File | Size (bytes) | Layout | Groups or records |
|---|---|---|---|
| GOG 2.1 `Zeus_Text.eng` | 262 936 | text | 366 |
| GOG 2.1 `Zeus_Editor_Text.eng` | 262 936 | text | 366 |
| GOG 2.1 `Zeus_MM.eng` | 595 160 | MM | 980 |
| 2.1 installation, French (the language pack) `Zeus_Text.eng` | 288 038 | text | 366 |
| same, `Zeus_Editor_Text.eng` | 262 936 | text | 366 |
| same, `Zeus_MM.eng` | 647 662 | MM | 980 |
| Poseidon cabinet: the same three | 288 038 / 262 936 / 647 662 | text, text, MM | |
| Zeus 1.x cabinet `Zeus_Text.eng` | 229 176 | text | 333 |
| Zeus 1.x cabinet `Zeus_MM.eng` | 484 818 | MM | 980 |
| editor archives, patch with a whole editor file | 216 178 to 288 038 | text | 333 to 366 |

## Not decoded

- The third header word of the text files (0x18).
- The meaning of the page-kind values and of the geometry fields of an MM record.
- The routine that follows `@` links in the help window.
- The strings themselves are the game's text and are not reproduced here.
