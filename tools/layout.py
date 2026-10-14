#!/usr/bin/env python3
"""Reference reader for the building plans in `Model/` (see formats/layout.md).

    Sanctuary_<GOD>.txt, Sanctuary_ZEUS_Pos.txt, Sanctuary_POSEIDON_GRAND.txt
    Poseidon_Pyramid_01..15.txt
    Palace.txt
    sanctuary_conan.txt, statue_conan.txt

A plan is a list of pieces `{x, y, type, elevation}` (pyramids add a fifth number, the build
order). The game reads it with `sscanf("%d,%d,%d,%d")` after each `{` found anywhere in the
file; everything else in the file is for humans. The reader does the same, then checks that the
file holds nothing else that looks like data.

Usage:
    layout.py info FILE
    layout.py check DIR...    read every plan below the directories; exit 1 on any failure
    layout.py dump FILE       one piece per line, tab-separated

Standard library only. Files are only ever opened for reading.
"""
import os
import re
import sys

SPLIT_TYPE = 1000          # pieces with type >= 1000 go to a second list (FUN_00540110)
_INT = r'\s*(-?\d+)\s*'
_PHASE_HEAD = re.compile(rb'^\{\s*(\d+)\s*\}\s*$')
_PHASE_ROW = re.compile(rb'^\{' + b','.join([_INT.encode()] * 10) + rb'\}\s*$')


def kind_of(path):
    n = os.path.basename(path).lower()
    if n == 'statue_conan.txt' or n == 'sanctuary_conan.txt':
        return 'phases'
    if n.startswith('poseidon_pyramid_'):
        return 'pyramid'
    if n.startswith('sanctuary_') or n == 'palace.txt':
        return 'plan'
    return None


_NUM = re.compile(rb'\s*(-?\d+)')


def scan_record(buf, pos, nfields, prev):
    """Like sscanf("%d,%d,%d,%d[,%d]") at `pos`. Returns (values, converted).

    The game does not look at the result of sscanf: a field that cannot be read keeps the value
    it had in the previous record (its variables live outside the loop), so a damaged record
    still produces a piece.
    """
    vals = list(prev)
    n = 0
    while n < nfields:
        m = _NUM.match(buf, pos)
        if m is None:
            break
        vals[n] = int(m.group(1))
        n += 1
        pos = m.end()
        if n < nfields:
            while pos < len(buf) and buf[pos] in b' \t\r\n':
                pos += 1
            if buf[pos:pos + 1] != b',':
                break
            pos += 1
    return tuple(vals), n


def read_plan(buf, fields):
    """Pieces as the game reads them: one per `{`, in file order. Also returns the number of
    records whose fields could not all be read."""
    pieces = []
    damaged = 0
    prev = (0,) * fields
    pos = buf.find(b'{')
    while pos >= 0:
        vals, n = scan_record(buf, pos + 1, fields, prev)
        if n < fields:
            damaged += 1
        pieces.append(vals)
        prev = vals
        pos = buf.find(b'{', pos + 1)
    return pieces, damaged


def read_phases(buf):
    """Phase tables: `; Phase n`, then `{count}`, then count rows of ten numbers."""
    phases = []
    want = None
    for raw in buf.split(b'\n'):
        line = raw.strip()
        if not line or line.startswith(b';'):
            continue
        m = _PHASE_HEAD.match(line)
        if m:
            if want:
                raise ValueError('phase %d is short of %d rows' % (len(phases) - 1, want))
            phases.append([])
            want = int(m.group(1))
            continue
        m = _PHASE_ROW.match(line)
        if m and phases and want:
            phases[-1].append(tuple(int(x) for x in m.groups()))
            want -= 1
            continue
        raise ValueError('unexpected line %r' % line[:40])
    if want:
        raise ValueError('last phase is short of %d rows' % want)
    return phases


def check_file(path):
    """Return (summary, problems)."""
    buf = open(path, 'rb').read()
    kind = kind_of(path)
    problems = []
    if kind == 'phases':
        try:
            ph = read_phases(buf)
        except ValueError as e:
            return '', [str(e)]
        return 'phases=%d rows=%d' % (len(ph), sum(len(p) for p in ph)), problems
    fields = 5 if kind == 'pyramid' else 4
    pieces, damaged = read_plan(buf, fields)
    opens = buf.count(b'{')
    if opens != len(pieces):
        problems.append('%d "{" but %d pieces' % (opens, len(pieces)))
    notes = []
    if damaged:
        notes.append('%d damaged' % damaged)
    if buf.count(b'}') != opens:
        notes.append('%d "{" / %d "}"' % (opens, buf.count(b'}')))
    # No data hiding outside braces: any other line starting with a digit or minus sign.
    for ln, raw in enumerate(buf.split(b'\n'), 1):
        s = raw.strip()
        if re.match(rb'^-?\d+\s*,', s):
            problems.append('line %d starts with a number outside braces' % ln)
            break
    main = [p for p in pieces if p[2] < SPLIT_TYPE]
    side = [p for p in pieces if p[2] >= SPLIT_TYPE]
    return ('pieces=%d (type>=%d: %d)%s' % (len(main), SPLIT_TYPE, len(side),
                                          ''.join(' [' + x + ']' for x in notes)), problems)


def walk(dirs):
    for d in dirs:
        for root, _, files in os.walk(d):
            for f in sorted(files):
                p = os.path.join(root, f)
                if kind_of(p):
                    yield p


def main(argv):
    if len(argv) >= 3 and argv[1] == 'info':
        s, p = check_file(argv[2])
        print(kind_of(argv[2]), s, 'OK' if not p else p)
        return 1 if p else 0
    if len(argv) >= 3 and argv[1] == 'check':
        n = bad = 0
        for p in walk(argv[2:]):
            s, probs = check_file(p)
            n += 1
            print('%-4s %s  %s' % ('OK' if not probs else 'FAIL', p, s))
            for q in probs[:5]:
                print('       ', q)
            bad += bool(probs)
        print('%d files, %d failed' % (n, bad))
        return 1 if bad or n == 0 else 0
    if len(argv) >= 3 and argv[1] == 'dump':
        buf = open(argv[2], 'rb').read()
        kind = kind_of(argv[2])
        if kind == 'phases':
            for i, ph in enumerate(read_phases(buf)):
                for row in ph:
                    print('\t'.join([str(i)] + [str(v) for v in row]))
        else:
            for p in read_plan(buf, 5 if kind == 'pyramid' else 4)[0]:
                print('\t'.join(str(v) for v in p))
        return 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
