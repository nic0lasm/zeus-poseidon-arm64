# Event messages — `Zeus eventmsg.txt` and `Zeus event message categories.txt`

Reference reader: `tools/eventmsg.py` (`check DIR… [--exe Zeus.exe]`, `dump FILE`; standard library
only). Addresses are in the GOG `Zeus.exe` 2.1. The files live in `Model/`.

Status as of 2026-10-14: **11 of 11 files read in full** (4 message files, 4 category files, 3
"attitude advice" files), by a re-implementation of the game's own reading code. With `--exe`, the
reader also checks every phrase name against the game's table of 4 172 names. Two further files
with the same names in the update patches (`patch-zeus-1.1`, `patch-poseidon-2.1`) are binary patch
fragments (262 and 143 bytes, containing NUL bytes), not text files; the reader does not look at
them, as for `.eng` ([eng.md](eng.md)).

Plain text, CRLF line endings. The English files (GOG) are ASCII; the French ones use one byte per
accented letter (Windows-1252 / Latin-1 range).

| File | Role | Read by |
|---|---|---|
| `Zeus eventmsg.txt` | the text of every message the game can show about events, by phrase name | `FUN_00505770`, called from `FUN_00466930` with the path at `0x006326F0` |
| `Zeus event message categories.txt` | default display level of 20 categories, and the category of each phrase | `FUN_00466B80`, called at start-up by `FUN_005C44C0` with the path at `0x00661AEC` |
| `Zeus attitude advice.txt` | 50 lines of advice about an ally's opinion of the player | not read by name: no string in the executable names it (verified by search); hypothesis: a design leftover or text copied elsewhere |

If the categories file cannot be opened, the game exits with a message (the string at
`0x00661A98` says the file was not found or too large). If the messages file cannot be opened, `FUN_00505770` raises no error and every text stays empty
(from the code).

## The phrase table (verified)

The game does not number messages in the file: each message has a **phrase name** and the
executable holds the list. It is an array of 8-byte entries `{u32 id, char *name}` at
`0x005FBC38`, ids 0 to 4171 in order, ended by a null name pointer: **4 172 phrases** in the GOG
executable (verified by reading the table; the entries are in id order). `FUN_00505AF0` turns a
name into an id (comparison without regard to case) and `FUN_00505B30` an id into its name. All
names start with `PHRASE_`.

Families, by the shape of the names in the table (verified on the table):

* every event has a **full** text and a **condensed** one: the condensed name is the full name
  with `CONDENSED_` after `PHRASE_` (1 980 of the 4 174 message lines in the GOG file);
* the parts of one message have their own phrase: title, initial announcement, reason (or "no
  reason"), oracle prediction, reminder, overdue notice, city-attacked alert, disembarked alert …;
* request messages come in four tones, marked by the last letters `_S`, `_D`, `_P`, `_R`
  (standard, demand, plea, requisition: the file's own comment);
* a few phrases are not messages but short fragments plugged into others (the "reason" phrases of
  automatic events, the size words of gifts).

## `Zeus eventmsg.txt`

A list of entries `PHRASE_name   "text"`. Whitespace between the name and the quote is free (tabs
and spaces). Reading rules, as implemented by `FUN_00505770` (two passes over the file in memory,
the first to size the text storage, the second to fill it; verified by reading the function, and
by the reader producing the same entries as the table lookup on all four files):

1. A `;` starts a comment that runs to the end of the line. Comments carry section titles and
   lists of the variables a message may use.
2. Any byte below `0x21` separates tokens.
3. A token that is **not** in the phrase table is skipped, whatever it looks like. This is how
   quotes inside a text are harmless (see below) and how a typo in a name silently drops a message.
4. A token that **is** in the table (case-insensitive) is a name. The next token must start with a
   double quote. The text runs up to the next double quote and may span lines; the bytes are copied
   as they are. If the token after a known name does not start with `"`, the game stops reading
   the whole file.
5. A backslash followed only by blanks and a line break joins the next line to the text; any other
   backslash is kept (hypothesis from the code: **no backslash occurs in the shipped files**).
6. A phrase defined twice keeps the later text.

**A double quote cannot appear inside a text.** The French 2.1 files contain texts where a
character is quoted inside the sentence; the text then ends at the inner quote and the rest of the
sentence is skipped token by token until the next known name. This is the game's actual behaviour,
the reader reproduces it.

### What is in the shipped files

| File | Entries | Distinct phrases | Notes |
|---|---|---|---|
| GOG / English 2.1 | 4 173 | 4 172 of 4 172 | all phrases defined; 178 are empty strings (or "not applicable" markers) |
| French 2.1 (`install`, `cab-poseidon`) | 4 173 | 4 172 of 4 172 | same layout |
| Zeus 1.x cabinet | 3 474 | 3 472 of 4 172 | 700 phrases of the 2.1 table are absent; 2 defined twice |

In all four files, one line names a phrase that is not in the game's table (a misspelled name, visible with
`check --exe`); the game skips it, so that message is
simply not defined by that line. One phrase (the "medusa slain" reason) is defined twice, the second
text wins.

### Variables in the text

Texts contain `[name]` placeholders that the game replaces when it shows the message. The names
come from a table of bare words in `.data` just before the file's path (about 42 names,
`shortgod` … `player_name`, `0x0063258C` … `0x006326C4`: verified that the words are there,
the substitution code itself is not traced). A GOG file uses 22 distinct placeholders (city,
leader and player names, nationality, amounts, items, time quantities, god, monster, hero needed,
gift size, reason phrase …), the French 2.1 file 23, the Zeus 1.x file 24. Square brackets are not
part of the name in the table.

## `Zeus event message categories.txt`

Lines starting with `;` and blank lines are ignored. `FUN_00466B80` then reads:

1. **20 default levels**, the first 20 data lines, each `N,level` where `N` must equal the line
   number counted from 0 (otherwise the load fails and the game exits). `level` is stored as a byte
   at `0x00916AE4 + N`. The comment says 0 = off, 1 = condensed, 2 = full (file's own comment;
   hypothesis for the meaning of the numbers). All 20 are 2 in the shipped files.
2. **One line per phrase**: `PHRASE_name,category,…`. The name ends at the first comma, the
   category is the integer after it and goes to byte `0x00915A98 + phrase id` (a table of 4 172
   bytes, first filled with `0xFF`). A name unknown to the game skips the line. Text after the
   category (a readable title of the category, empty columns) is never read.
3. **Fill**: afterwards every phrase that no line mentioned takes the category of the phrase with
   the preceding id (the first one takes 0). So a file lists only the first phrase of each run.

Shipped files: 459 lines (GOG, English, French 2.1), 454 (Poseidon cabinet), 418 (Zeus 1.x), using
18 distinct category numbers within 0 to 19, where 17 and 18 never occur. Category numbers 0 to 19
address the table of default levels: that makes the category the player-visible message class whose
display level the options screen controls (hypothesis: the options code was not traced).

## `Zeus attitude advice.txt`

50 non-blank lines, 5 of them `;` headings, the others one paragraph each, about the opinion an
ally has of the player. In English in every copy (the three copies — GOG, `install`, `cab-zeus` —
have the same size, 6 158 bytes). Only checked for being plain text lines. The game's use is unknown (see above).

## Not covered

* The substitution code and the options screen that sets display levels (not traced).
* The `.eng` text files: [eng.md](eng.md). Simulation tables and plans of `Model/`:
  [model.md](model.md), [layout.md](layout.md).

## Tested files

All 11 files named above in `install/Model`, `gog/app/Model`, `cab-zeus/Model`, `cab-poseidon/Model`
(`python3 tools/eventmsg.py check original --exe original/gog/app/Zeus.exe`): 0 failures.
