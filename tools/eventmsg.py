#!/usr/bin/env python3
"""Reference reader for the event message files of `Model/` (see formats/eventmsg.md).

    Zeus eventmsg.txt                    the text of every event message, by phrase name
    Zeus event message categories.txt    default display level per category, category per phrase

The reader re-implements what the game does (FUN_00505770 for the messages, FUN_00466B80 for the
categories), so that "read in full" means "read the way the game reads it".

    eventmsg.py check DIR... [--exe Zeus.exe]   read every file below the directories
    eventmsg.py dump FILE                       one entry per line (text with \\n escapes)

With `--exe` (a GOG Zeus.exe, used read-only) the phrase names are checked against the game's own
table of 4 172 names (at 0x005FBC38); without it names are only checked for consistency.
Standard library only.
"""
import os
import re
import struct
import sys

NAME_TABLE_VA = 0x005FBC38      # {u32 id, char *name} entries, ends with a null name pointer
N_DEFAULT_LEVELS = 20           # first 20 data lines of the categories file (FUN_00466B80)


def phrase_table(exe_path):
    """Phrase names in the game's order (id = position), from a Zeus.exe."""
    d = open(exe_path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec, = struct.unpack_from('<H', d, pe + 6)
    opt, = struct.unpack_from('<H', d, pe + 20)
    base, = struct.unpack_from('<I', d, pe + 24 + 28)
    secs = []
    for i in range(nsec):
        _, vs, va, rs, ro = struct.unpack_from('<8sIIII', d, pe + 24 + opt + i * 40)
        secs.append((va, max(vs, rs), ro))

    def off(v):
        for va, size, ro in secs:
            if va <= v - base < va + size:
                return ro + v - base - va
        raise ValueError('address %#x outside the sections' % v)

    names = []
    a = NAME_TABLE_VA
    while True:
        ident, ptr = struct.unpack_from('<II', d, off(a))
        if ptr == 0:
            break
        if ident != len(names):
            raise ValueError('name table is not numbered in order at entry %d' % len(names))
        o = off(ptr)
        names.append(d[o:d.index(b'\0', o)].decode('latin-1'))
        a += 8
    return names


# --------------------------------------------------------------------------- messages

def parse_messages(buf, known=None):
    """Entries as the game reads them: list of (name, text); anomalies as a list of strings.

    Rules (FUN_00505770): `;` starts a comment to the end of the line; bytes below 0x21 separate
    tokens; a token is looked up (case-insensitively) in the game's table of phrase names. A token
    that is not in the table is skipped, whatever it is (this is how stray quotes inside a text and
    the lists of variables in the comments are ignored). A known name must be followed by a
    token starting with `"`, which opens the text, up to the next `"` (line breaks allowed).
    A backslash followed only by blanks and a line break splices the lines; any other backslash
    is kept. If something other than `"` follows a known name, the game stops reading the file.

    `known` is the set of lower-case phrase names (see `phrase_table`). Without it, a name is any
    token starting with `PHRASE_`; `check --exe` verifies on the shipped files that both give the
    same entries.
    """
    entries = []
    anomalies = []
    pos, n = 0, len(buf)
    pending = None
    while pos < n:
        c = buf[pos]
        if c == 0x3b:                              # ;
            while pos < n and buf[pos] not in b'\r\n':
                pos += 1
        elif c < 0x21:
            pos += 1
        elif pending is None:
            end = pos
            while end < n and buf[end] >= 0x21:
                end += 1
            tok = buf[pos:end].decode('latin-1')
            if known is not None:
                pending = tok if tok.lower() in known else None
            else:
                pending = tok if tok.lower().startswith('phrase_') else None
            pos = end
        elif c == 0x22:                            # "
            pos += 1
            out = bytearray()
            while pos < n and buf[pos] != 0x22:
                if buf[pos] == 0x5c:               # backslash
                    j = pos + 1
                    while j < n and buf[j] < 0x21 and buf[j] not in b'\r\n':
                        j += 1
                    if j < n and buf[j] in b'\r\n':
                        j += 1
                        if j < n and buf[j] in b'\r\n' and buf[j] != buf[j - 1]:
                            j += 1
                        pos = j
                        continue
                out.append(buf[pos])
                pos += 1
            if pos >= n:
                anomalies.append('unterminated text for %s' % pending)
                break
            pos += 1
            entries.append((pending, bytes(out)))
            pending = None
        else:
            anomalies.append('stopped at byte %d: %s is not followed by a quote' % (pos, pending))
            break
    if pending is not None and not anomalies:
        anomalies.append('name %s without text at the end' % pending)
    return entries, anomalies


# --------------------------------------------------------------------------- categories

def parse_categories(buf, names=None):
    """Returns (defaults, per_phrase, problems).

    `defaults` are the 20 display levels (0 off, 1 condensed, 2 full) of categories 0 to 19;
    `per_phrase` maps phrase name -> category number. After the defaults every line is
    `PHRASE_name,category,...`; names the game does not know are skipped silently.
    """
    defaults = []
    per_phrase = {}
    problems = []
    lookup = {n.lower() for n in names} if names else None
    for raw in buf.split(b'\n'):
        line = raw.strip()
        if not line or line.startswith(b';'):
            continue
        if len(defaults) < N_DEFAULT_LEVELS:
            m = re.match(rb'^(\d+),(\d+)', line)
            if m is None or int(m.group(1)) != len(defaults):
                problems.append('default level line %d is not "%d,level"' % (len(defaults), len(defaults)))
                break
            defaults.append(int(m.group(2)))
            continue
        m = re.match(rb'^([^,]*),\s*(\d+)', line)
        if m is None:
            problems.append('line without a category: %r' % line[:40])
            continue
        name = m.group(1).decode('latin-1')
        if lookup is not None and name.lower() not in lookup:
            problems.append('unknown phrase %s (the game skips it)' % name)
            continue
        per_phrase[name] = int(m.group(2))
    return defaults, per_phrase, problems


# --------------------------------------------------------------------------- driver

def kind_of(path):
    n = os.path.basename(path).lower()
    if n == 'zeus eventmsg.txt':
        return 'messages'
    if n == 'zeus event message categories.txt':
        return 'categories'
    if n == 'zeus attitude advice.txt':
        return 'advice'
    return None


def check_file(path, names=None):
    buf = open(path, 'rb').read()
    kind = kind_of(path)
    problems = []
    if kind == 'messages':
        known = {x.lower() for x in names} if names is not None else None
        entries, anomalies = parse_messages(buf, known)
        problems += anomalies
        notes = []
        if known is not None:
            loose = parse_messages(buf)[0]
            extra = [n for n, _ in loose if n.lower() not in known]
            if [e for e in loose if e[0].lower() in known] != entries:
                problems.append('the table-free reading differs from the table reading')
            if extra:
                notes.append('%d line(s) name a phrase the game does not know and are skipped: %s'
                             % (len(extra), ', '.join(sorted(set(extra)))))
        distinct = {n.lower() for n, _ in entries}
        summary = 'entries=%d distinct=%d' % (len(entries), len(distinct))
        if names is not None:
            summary += ' of %d phrases' % len(names)
        # every line that names a phrase must give an entry (nothing silently skipped)
        wanted = len(re.findall(rb'(?im)^[ \t]*PHRASE_\S+[ \t]+"', buf))
        if wanted != len(entries) + len(notes and extra or []):
            problems.append('%d lines look like entries, %d read' % (wanted, len(entries)))
        dup = len(entries) - len(distinct)
        if dup:
            notes.append('%d phrase defined twice (the later text wins)' % dup)
        return summary + ''.join('  [' + x + ']' for x in notes), problems
    if kind == 'categories':
        defaults, per, probs = parse_categories(buf, names)
        problems += probs
        if len(defaults) != N_DEFAULT_LEVELS:
            problems.append('only %d default levels' % len(defaults))
        return 'levels=%d phrases=%d categories=%d' % (
            len(defaults), len(per), len(set(per.values()))), problems
    if kind == 'advice':
        lines = [l for l in buf.decode('latin-1').splitlines() if l.strip()]
        head = [l for l in lines if l.startswith(';')]
        return 'lines=%d (comments %d)' % (len(lines), len(head)), problems
    return '', ['not an event message file']


def walk(dirs):
    for d in dirs:
        for root, _, files in os.walk(d):
            for f in sorted(files):
                p = os.path.join(root, f)
                if kind_of(p):
                    yield p


def main(argv):
    args = argv[1:]
    exe = None
    if '--exe' in args:
        i = args.index('--exe')
        exe = args[i + 1]
        del args[i:i + 2]
    names = phrase_table(exe) if exe else None
    if len(args) >= 2 and args[0] == 'check':
        n = bad = 0
        for p in walk(args[1:]):
            s, probs = check_file(p, names)
            n += 1
            print('%-4s %s  %s' % ('OK' if not probs else 'FAIL', p, s))
            for q in probs[:5]:
                print('       ', q)
            bad += bool(probs)
        print('%d files, %d failed' % (n, bad))
        return 1 if bad or n == 0 else 0
    if len(args) >= 2 and args[0] == 'dump':
        buf = open(args[1], 'rb').read()
        if kind_of(args[1]) == 'messages':
            for name, text in parse_messages(buf)[0]:
                print(name + '\t' + text.decode('latin-1').replace('\r', '').replace('\n', '\\n'))
        else:
            d, per, _ = parse_categories(buf)
            print('defaults', *d)
            for k, v in per.items():
                print(k, v, sep='\t')
        return 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
