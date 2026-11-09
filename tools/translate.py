#!/usr/bin/env python3
"""Translate the game's texts through PO files, and build a language pack from them.

    translate.py export GAME_DIR PO_DIR [--language CODE]
        Writes one PO file per text set from the game's English texts. Run again on a folder
        that already holds translations: they are kept, and an entry whose English text changed
        is marked fuzzy.
    translate.py build GAME_DIR PO_DIR PACK_DIR [--lang-dir DIR] [--code-page N] [--table FILE]
        Writes the translated texts into PACK_DIR (Zeus_Text.eng, Zeus_Editor_Text.eng,
        Zeus_MM.eng, Model/Zeus eventmsg.txt and the changed Adventures/*/*.txt), plus
        codage.txt when the code page is not 1252 and an empty remplace.txt. The code page and
        the alphabetical order of the help's table of contents come from DIR/alphabet.txt
        (keys `code_page` and `order`, see tools/examples/lang/uk), or --code-page; the
        character table from --table or PACK_DIR/table-caracteres.bin (see tools/alphabet.py),
        used only to warn about characters that have no glyph.
    translate.py stats PO_DIR
        How much is translated, set by set.

PO files (msgctxt identifies each string; msgid is the English text):
    Zeus_Text.po         "group.index" (the interface, about 8,200 strings)
    Zeus_Editor_Text.po  only the strings that differ from Zeus_Text (the rest is taken there)
    Zeus_MM.po           "record.title|caption|body" (the in-game help, codes @P @L @G… @number)
    eventmsg.po          "PHRASE_…" (event messages, variables such as [city_name])
    Adventures.po        "Folder/File.txt|Key" (titles and texts of the adventures)

An empty msgstr, or a fuzzy entry, keeps the English text. Checked while building (an error
stops the build, a warning is only printed): errors — a character outside the code page, the
variables [...], %s/%d or @ codes not matching the English text, bytes below 0x20; warnings —
a character without a glyph in the character table, a translation much longer than the original,
double quotes in an event message or an adventure (the game would end the text there; they are
replaced by single quotes).

PO files hold the game's text, and a pack is made of game files: like every language pack
(docs/languages.md) they stay on the translator's computer and must not be shared.
"""
import os
import re
import struct
import sys
import unicodedata

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import engtext  # noqa: E402
import eventmsg  # noqa: E402

SRC_CODEPAGE = "cp1252"

# Typographic characters the game's fonts do not have, and what replaces them.
REPLACE = {
    "\u201c": '"', "\u201d": '"', "\u201e": '"', "\u00ab": '"', "\u00bb": '"',
    "\u2018": "'", "\u2019": "'", "\u02bc": "'",
    "\u2014": "-", "\u2013": "-", "\u2011": "-",
    "\u2026": "...",
    "\u00a0": "_",   # a no-break space: the game writes it "_"
    "\u2116": "No",
}
SETS = ("Zeus_Text", "Zeus_Editor_Text", "Zeus_MM", "eventmsg", "Adventures")


# --- PO files --------------------------------------------------------------------------------

def po_quote(s):
    s = s.replace("\\", "\\\\").replace('"', '\\"').replace("\t", "\\t").replace("\n", "\\n")
    return '"' + s + '"'


def po_unquote(s):
    out, i = [], 0
    s = s[1:-1]
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            n = s[i + 1]
            out.append({"n": "\n", "t": "\t", '"': '"', "\\": "\\"}.get(n, "\\" + n))
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


class Entry:
    def __init__(self, ctxt, msgid, msgstr="", comments=(), fuzzy=False):
        self.ctxt, self.msgid, self.msgstr = ctxt, msgid, msgstr
        self.comments = list(comments)  # "#. …" lines (notes for the translator, rewritten on export)
        self.translator = []            # "# …" lines (the translator's own notes, kept)
        self.fuzzy = fuzzy


def read_po(path):
    entries, cur, field = [], None, None
    pending_comments, pending_translator, fuzzy = [], [], False

    def flush():
        nonlocal cur
        if cur is not None and cur.ctxt is not None:
            entries.append(cur)
        cur = None

    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.rstrip("\n")
            if not line.strip():
                flush()
                field = None
                continue
            if line.startswith("#,"):
                fuzzy = "fuzzy" in line
                continue
            if line.startswith("#."):
                pending_comments.append(line[2:].strip())
                continue
            if line.startswith("#"):
                if not line.startswith("#~") and not line.startswith("#:") and not line.startswith("#|"):
                    pending_translator.append(line[1:].strip())
                continue
            key, _, rest = line.partition(" ")
            if key in ("msgctxt", "msgid", "msgstr"):
                if key == "msgctxt" or (key == "msgid" and (cur is None or cur.msgid is not None)):
                    flush()
                if cur is None:
                    cur = Entry(None, None)
                    cur.comments, cur.translator, cur.fuzzy = pending_comments, pending_translator, fuzzy
                    pending_comments, pending_translator, fuzzy = [], [], False
                setattr(cur, key if key != "msgctxt" else "ctxt", po_unquote(rest.strip()))
                field = key
            elif line.startswith('"') and cur is not None and field:
                attr = "ctxt" if field == "msgctxt" else field
                setattr(cur, attr, getattr(cur, attr) + po_unquote(line.strip()))
    flush()
    return [e for e in entries if e.ctxt]


def write_po(path, entries, title, language):
    with open(path, "w", encoding="utf-8") as f:
        f.write("# %s: translation of Zeus + Poseidon. It holds the game's text: do not share it.\n" % title)
        f.write('msgid ""\nmsgstr ""\n"Content-Type: text/plain; charset=UTF-8\\n"\n')
        if language:
            f.write('"Language: %s\\n"\n' % language)
        f.write("\n")
        for e in entries:
            for c in e.translator:
                f.write("# %s\n" % c)
            for c in e.comments:
                f.write("#. %s\n" % c)
            if e.fuzzy:
                f.write("#, fuzzy\n")
            f.write("msgctxt %s\n" % po_quote(e.ctxt))
            f.write("msgid %s\n" % po_quote(e.msgid))
            f.write("msgstr %s\n\n" % po_quote(e.msgstr))


# --- the game's texts (read only) ------------------------------------------------------------

def dec(raw):
    return raw.decode(SRC_CODEPAGE, errors="replace")


def read(game, *parts):
    with open(os.path.join(game, *parts), "rb") as f:
        return f.read()


def adventure_files(game):
    root = os.path.join(game, "Adventures")
    for folder in sorted(os.listdir(root)):
        d = os.path.join(root, folder)
        if os.path.isdir(d):
            for name in sorted(os.listdir(d)):
                if name.lower().endswith(".txt"):
                    yield os.path.join(folder, name)


def adventure_entries(game, rel=None):
    """(context, start, end, text) of each Key="text" line of the adventures' .txt files."""
    out = []
    for r in ([rel] if rel else adventure_files(game)):
        data = read(game, "Adventures", r)
        for m in re.finditer(rb'(?m)^([A-Za-z_0-9]+)="([^"]*)"', data):
            out.append(("%s|%s" % (r.replace(os.sep, "/"), m.group(1).decode()), m.start(2), m.end(2), dec(m.group(2))))
    return out


def source_entries(game):
    """{set: [(context, English text, comment)]} from the game's files."""
    out = {}
    tf = engtext.TextFile(read(game, "Zeus_Text.eng"))
    out["Zeus_Text"] = [("%d.%d" % (g, i), dec(s), "") for g, ss in sorted(tf.groups.items())
                        for i, s in enumerate(ss) if s]
    ed = engtext.TextFile(read(game, "Zeus_Editor_Text.eng"))
    out["Zeus_Editor_Text"] = [("%d.%d" % (g, i), dec(s), "") for g, ss in sorted(ed.groups.items())
                               for i, s in enumerate(ss)
                               if s and (g not in tf.groups or i >= len(tf.groups[g]) or tf.groups[g][i] != s)]
    mm = engtext.MMFile(read(game, "Zeus_MM.eng"))
    rows = []
    for r in range(mm.used):
        for name, k in (("title", 17), ("caption", 18), ("body", 19)):
            t = mm.text(r, k)
            if t:
                rows.append(("%d.%s" % (r, name), dec(t), ""))
    out["Zeus_MM"] = rows
    entries, _ = eventmsg.parse_messages(read(game, "Model", "Zeus eventmsg.txt"))
    seen, rows = {}, []
    for name, text in entries:
        seen[name] = seen.get(name, 0) + 1
        ctxt = name if seen[name] == 1 else "%s/%d" % (name, seen[name])
        if text:
            rows.append((ctxt, dec(text), ""))
    out["eventmsg"] = rows
    out["Adventures"] = [(ctxt, value.replace("\r\n", "\n"), "") for ctxt, _, _, value in adventure_entries(game)
                         if value.strip() and value.strip() != "n/a"]
    return out


# --- checks ----------------------------------------------------------------------------------

def tokens(text, kind):
    """What a translation must keep: [variables], %s/%d, and the help's @ codes."""
    variables = sorted(re.findall(r"\[[A-Za-z_]+\]", text))
    formats = re.findall(r"%[sd]", text)
    codes = sorted(re.findall(r"@(?:P|L|G\d+|\d+)", text)) if kind in ("Zeus_MM", "Zeus_Text", "Zeus_Editor_Text", "Adventures") else []
    return variables, formats, codes


def encode(text, codepage):
    """Unicode -> the game's bytes; returns (bytes, [characters with no byte])."""
    out, bad = bytearray(), []
    for ch in text:
        try:
            out += ch.encode(codepage)
            continue
        except UnicodeEncodeError:
            pass
        rep = REPLACE.get(ch)
        if rep is None:
            base = unicodedata.normalize("NFKD", ch)
            rep = "".join(c for c in base if not unicodedata.combining(c))
        try:
            out += rep.encode(codepage)
        except UnicodeEncodeError:
            bad.append(ch)
            out += b"?"
    return bytes(out), bad


class Builder:
    def __init__(self, codepage, table):
        self.codepage, self.table = codepage, table
        self.errors, self.warnings = [], []

    def convert(self, kind, ctxt, english, translated, check=True):
        text = translated if translated else english
        # In some code pages but never in the fonts: always replaced (the fonts have one apostrophe).
        for a, b in (("\u2019", "'"), ("\u2018", "'"), ("\u2026", "..."), ("\u00a0", "_"),
                     ("\u2116", "No"), ("\u00a9", "(c)"), ("\u20ac", "EUR")):
            text = text.replace(a, b)
        if translated and check:
            if tokens(english, kind) != tokens(text, kind):
                self.errors.append("%s %s: variables or codes differ from the English text: %s -> %s"
                                   % (kind, ctxt, tokens(english, kind), tokens(text, kind)))
            if len(text) > max(len(english) * 1.6, len(english) + 12):
                self.warnings.append("%s %s: translation longer than the original (%d > %d)" % (kind, ctxt, len(text), len(english)))
        if kind in ("eventmsg", "Adventures") and '"' in text:
            text = text.replace('"', "'")
            self.warnings.append("%s %s: double quotes replaced by '" % (kind, ctxt))
        data, bad = encode(text, self.codepage)
        if bad:
            (self.errors if translated else self.warnings).append(
                "%s %s: characters outside the code page: %s" % (kind, ctxt, "".join(sorted(set(bad)))))
        if any(b < 0x20 and b not in (9, 10, 13) for b in data):
            self.errors.append("%s %s: control bytes" % (kind, ctxt))
        if self.table:
            missing = {chr(b) if b < 0x80 else bytes([b]).decode(self.codepage, "replace")
                       for b in data if b >= 0x20 and b not in (b"#@_[]&" if kind == "Zeus_MM" else b"#@_[]") and not self.table[b - 0x20] and b != 0x20}
            if missing and translated:
                self.warnings.append("%s %s: no glyph for %s" % (kind, ctxt, "".join(sorted(missing))))
        return data


def rebuild_text(tf):
    """Like engtext.TextFile.rebuild, for changed strings: an empty group gets the offset of the
    next group (not its old one), and dead bytes between groups are dropped."""
    n = tf.group_count
    blob, table = bytearray(), [(0, 0)] * engtext.TEXT_TABLE_ENTRIES
    for g in range(1, n):
        count = tf.table[g][1]
        if count == 0:
            continue
        table[g] = (len(blob), count)
        for s in tf.groups[g]:
            blob += s + b"\0"
    end = len(blob)
    for g in range(n - 1, 0, -1):  # empty groups: the start of the next one
        if tf.table[g][1] == 0:
            table[g] = (end, 0)
        else:
            end = table[g][0]
    blob += tf.tail
    head = engtext.TEXT_MAGIC + struct.pack("<3I", n, sum(c for _, c in table), tf.header_extra)
    return head + b"".join(struct.pack("<2I", o, c) for o, c in table) + bytes(blob)


def message_spans(buf):
    """(name, start, end) of the text of each message, read as the game reads them
    (eventmsg.parse_messages)."""
    spans, pos, n, pending = [], 0, len(buf), None
    while pos < n:
        c = buf[pos]
        if c == 0x3B:
            while pos < n and buf[pos] not in b"\r\n":
                pos += 1
        elif c < 0x21:
            pos += 1
        elif pending is None:
            end = pos
            while end < n and buf[end] >= 0x21:
                end += 1
            tok = buf[pos:end].decode("latin-1")
            pending = tok if tok.lower().startswith("phrase_") else None
            pos = end
        elif c == 0x22:
            start = pos + 1
            end = buf.index(b'"', start)
            spans.append((pending, start, end))
            pending = None
            pos = end + 1
        else:
            break
    return spans


def sort_key(order):
    """Alphabetical order of a language: the letters of `order` first, in that order, then
    every other character by its code point."""
    rank = {c: i for i, c in enumerate(order)}

    def key(label):
        return [(0, rank[c]) if c in rank else (1, ord(c)) for c in label.lower()]
    return key


def toc_text(mm, mm_tr, order):
    """The help's table of contents (record 1) in the language's alphabetical order: the same
    links as the original, a blank line between letters. Each label is the translated label of
    record 1's body when it was translated link by link, otherwise the translated title of the
    page the link leads to, otherwise the English label."""
    src = dec(mm.text(1, 19))
    links = re.findall(r"@(\d+)([^@\s]*)", src)
    done = re.findall(r"@(\d+)([^@\s]*)", mm_tr.get("1.body", ""))

    def norm(x):
        return x.replace("&", " ").strip().lower()
    entries = set()
    for k, (n, label) in enumerate(links):
        en_title = dec(mm.text(int(n), 17) or b"")
        tr_title = mm_tr.get("%s.title" % n)
        if tr_title and norm(label) == norm(en_title):
            title = tr_title  # the entry is the page's title
        elif len(done) == len(links) and done[k][0] == n:
            title = done[k][1].replace("&", " ")  # a synonym translated in 1.body
        else:
            title = label.replace("&", " ")
        entries.add((title.strip(), int(n)))
    key = sort_key(order)
    parts, last = [(mm_tr.get("1.title") or "Table of Contents") + " @L"], None
    for title, n in sorted(entries, key=lambda t: (key(t[0]), t[1])):
        first = title[:1].lower()
        if last is not None and first != last:
            parts.append("@L")
        parts.append("@%d%s @L" % (n, title.replace(" ", "&")))
        last = first
    return "".join(parts)


def load_translations(folder, name):
    path = os.path.join(folder, name + ".po")
    if not os.path.exists(path):
        return {}
    return {e.ctxt: (e.msgstr if not e.fuzzy else "") for e in read_po(path)}


def language_settings(lang_dir):
    """`code_page` and `order` from LANG_DIR/alphabet.txt (key: value lines, # comments)."""
    settings = {}
    path = os.path.join(lang_dir, "alphabet.txt") if lang_dir else None
    if path and os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.split("#", 1)[0].strip() if not line.lstrip().startswith("table") else line.strip()
                key, sep, value = line.partition(":")
                if sep:
                    settings[key.strip()] = value.strip()
    return settings


# --- commands --------------------------------------------------------------------------------

def cmd_export(game, folder, language):
    os.makedirs(folder, exist_ok=True)
    for name, rows in source_entries(game).items():
        path = os.path.join(folder, name + ".po")
        old = {e.ctxt: e for e in read_po(path)} if os.path.exists(path) else {}
        entries = []
        for ctxt, english, _ in rows:
            e = Entry(ctxt, english)
            if ctxt in old:
                o = old[ctxt]
                e.msgstr, e.translator = o.msgstr, o.translator
                e.fuzzy = o.fuzzy or (o.msgid != english and bool(o.msgstr))
            variables, formats, codes = tokens(english, name)
            notes = ["length %d" % len(english)]
            if variables or formats or codes:
                notes.append("keep: " + " ".join(variables + formats + codes))
            e.comments = ["; ".join(notes)]
            entries.append(e)
        write_po(path, entries, name, language)
        done = sum(1 for e in entries if e.msgstr and not e.fuzzy)
        print("%s: %d strings, %d translated" % (path, len(entries), done))


def cmd_build(game, folder, pack, codepage, table, order):
    b = Builder(codepage, table)
    tf_raw = read(game, "Zeus_Text.eng")
    text_tr = load_translations(folder, "Zeus_Text")
    base = engtext.TextFile(tf_raw).groups

    def build_text(raw, kind, own):
        tf = engtext.TextFile(raw)
        for g, ss in tf.groups.items():
            for i, s in enumerate(ss):
                if not s:
                    continue
                ctxt = "%d.%d" % (g, i)
                tr = own.get(ctxt, "")
                # The editor: a string identical to Zeus_Text's takes its translation from there.
                if not tr and kind != "Zeus_Text" and g in base and i < len(base[g]) and base[g][i] == s:
                    tr = text_tr.get(ctxt, "")
                ss[i] = b.convert(kind, ctxt, dec(s), tr)
        return rebuild_text(tf)

    out = {"Zeus_Text.eng": build_text(tf_raw, "Zeus_Text", text_tr)}
    out["Zeus_Editor_Text.eng"] = build_text(read(game, "Zeus_Editor_Text.eng"), "Zeus_Editor_Text",
                                             load_translations(folder, "Zeus_Editor_Text"))

    mm = engtext.MMFile(read(game, "Zeus_MM.eng"))
    mm_tr = load_translations(folder, "Zeus_MM")
    # The help's strings are stored one after another in record order: the text area is rebuilt.
    new_strings, pos, records = {}, engtext.MM_BLOB_LEAD, [list(r) for r in mm.records]
    for r in range(mm.used):
        for name, k in (("title", 17), ("caption", 18), ("body", 19)):
            t = mm.text(r, k)
            if t is None:
                continue
            ctxt = "%d.%s" % (r, name)
            if ctxt == "1.body":
                data = b.convert("Zeus_MM", ctxt, "", toc_text(mm, mm_tr, order), check=False)
            else:
                data = b.convert("Zeus_MM", ctxt, dec(t), mm_tr.get(ctxt, "")) if t else t
            records[r][k] = pos
            new_strings[pos] = data
            pos += len(data) + 1
    mm.records = [tuple(r) for r in records]
    mm.strings = new_strings
    out["Zeus_MM.eng"] = mm.rebuild()

    # Event messages: only the bytes between quotes change; comments and names stay as they were.
    src = read(game, "Model", "Zeus eventmsg.txt")
    ev_tr = load_translations(folder, "eventmsg")
    pieces, last, seen = [], 0, {}
    for name, start, end in message_spans(src):
        seen[name] = seen.get(name, 0) + 1
        ctxt = name if seen[name] == 1 else "%s/%d" % (name, seen[name])
        english = src[start:end]
        if b"\\" in english or not english:
            continue  # joined lines and empty texts stay as they are
        pieces.append(src[last:start])
        pieces.append(b.convert("eventmsg", ctxt, dec(english), ev_tr.get(ctxt, "")))
        last = end
    pieces.append(src[last:])
    ev = b"".join(pieces)
    check, anomalies = eventmsg.parse_messages(ev)
    if anomalies or len(check) != len(eventmsg.parse_messages(src)[0]):
        b.errors.append("eventmsg: the file no longer reads like the original: %s" % anomalies[:3])

    # The adventures' texts, in their own folders: Key="text", CRLF lines.
    adv_tr = load_translations(folder, "Adventures")
    adv_out = {}
    for rel in adventure_files(game):
        src_adv = read(game, "Adventures", rel)
        pieces, last, changed = [], 0, False
        for ctxt, start, end, value in adventure_entries(game, rel):
            tr = adv_tr.get(ctxt, "")
            if not tr:
                continue
            pieces.append(src_adv[last:start])
            data = b.convert("Adventures", ctxt, value.replace("\r\n", "\n"), tr.replace("\r\n", "\n"))
            pieces.append(data.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))
            last, changed = end, True
        if changed:
            pieces.append(src_adv[last:])
            adv_out[rel] = b"".join(pieces)

    for w in b.warnings:
        print("warning:", w)
    for e in b.errors:
        print("ERROR:", e)
    if b.errors:
        raise SystemExit("build stopped: %d errors" % len(b.errors))
    os.makedirs(os.path.join(pack, "Model"), exist_ok=True)
    for name, data in out.items():
        with open(os.path.join(pack, name), "wb") as f:
            f.write(data)
        engtext.load(data)  # read back: the format is whole
    with open(os.path.join(pack, "Model", "Zeus eventmsg.txt"), "wb") as f:
        f.write(ev)
    for rel, data in adv_out.items():
        os.makedirs(os.path.join(pack, "Adventures", os.path.dirname(rel)), exist_ok=True)
        with open(os.path.join(pack, "Adventures", rel), "wb") as f:
            f.write(data)
    if codepage != "cp1252":
        with open(os.path.join(pack, "codage.txt"), "w") as f:
            f.write("%s\n" % codepage[2:])
    if not os.path.exists(os.path.join(pack, "remplace.txt")):
        open(os.path.join(pack, "remplace.txt"), "w").close()
    print("built: %s (%d warnings)" % (pack, len(b.warnings)))


def cmd_stats(folder):
    for name in SETS:
        path = os.path.join(folder, name + ".po")
        if not os.path.exists(path):
            continue
        es = read_po(path)
        done = [e for e in es if e.msgstr and not e.fuzzy]
        words = sum(len(e.msgid.split()) for e in es)
        dwords = sum(len(e.msgid.split()) for e in done)
        print("%-17s %5d/%5d strings, %6d/%6d words (%.1f%%)" % (name, len(done), len(es), dwords, words,
                                                               100.0 * dwords / max(words, 1)))


def option(args, name, default=None):
    return args[args.index(name) + 1] if name in args else default


def main(argv):
    if len(argv) >= 4 and argv[1] == "export":
        cmd_export(argv[2], argv[3], option(argv[4:], "--language", ""))
    elif len(argv) >= 5 and argv[1] == "build":
        game, folder, pack, args = argv[2], argv[3], argv[4], argv[5:]
        settings = language_settings(option(args, "--lang-dir"))
        codepage = "cp" + option(args, "--code-page", settings.get("code_page", "1252"))
        table_path = option(args, "--table", os.path.join(pack, "table-caracteres.bin"))
        table = None
        if os.path.exists(table_path):
            with open(table_path, "rb") as f:
                table = f.read()
        cmd_build(game, folder, pack, codepage, table, settings.get("order", ""))
    elif len(argv) >= 3 and argv[1] == "stats":
        cmd_stats(argv[2])
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
