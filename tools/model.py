#!/usr/bin/env python3
"""Reference reader for the simulation tables in `Model/` (see formats/model.md).

Covers the four families of difficulty-dependent tables:

    Zeus_Model_<level>.txt        buildings and houses
    Figure_model_<level>.txt      walkers, soldiers and enemy units
    Tax_Sentiment_Model.txt       tax rate -> sentiment, one column per level
    monster_behavior_<level>.txt  monster strength per level

The reader does two independent things and requires them to agree:

  1. `game_view`: the game's own algorithm, re-implemented. It finds a section marker as a
     substring, then for each record jumps to the next `{`, and for each field skips to the
     next digit or minus sign, reads an integer and jumps past the next comma
     (decimals are therefore truncated, extra columns are never read).
  2. `parse`: a line-based reader that also keeps the labels, for humans and tools.

Usage:
    model.py info FILE            summary of one file
    model.py check DIR...         read every table below the directories; exit 1 on any failure
    model.py dump FILE            records as tab-separated values

Standard library only. Files are only ever opened for reading.
"""
import os
import re
import sys

# Field counts and record counts. Source: loader functions of Zeus.exe (GOG 2.1) at
# 0x005B0C20 (Zeus_Model), 0x005B0E60 (Figure_model), 0x005B10F0 (Tax_Sentiment_Model);
# see formats/model.md.
BUILDING_FIELDS = 10
HOUSE_FIELDS = 21
FIGURE_FIELDS = 14
TAX_ROWS = 7
MONSTER_FIELDS = 8      # FUN_0052F460 reads 8 of the 9 numbers of a row; it expects 16 rows
TAX_COLUMNS = 6          # rate, then one value per difficulty level
MAX_RECORDS = 299        # the loops of the game stop after 299 iterations
SKIP_LIMIT = 1000        # FUN_00508030 gives up after 1000 characters


def kind_of(path):
    name = os.path.basename(path).lower()
    if name.startswith('zeus_model_'):
        return 'zeus_model'
    if name.startswith('figure_model_'):
        return 'figure_model'
    if name.startswith('monster_behavior_'):
        return 'monster'
    if name == 'tax_sentiment_model.txt':
        return 'tax'
    return None


def level_of(path):
    name = os.path.basename(path).lower()
    m = re.match(r'.*_([a-z]+)\.txt$', name)
    return m.group(1) if m else None


# --------------------------------------------------------------------------- game's view

def _skip_to_number(buf, pos):
    """FUN_00508030: advance to the first digit or '-' (at most SKIP_LIMIT characters)."""
    n = 0
    while n < SKIP_LIMIT - 1 and pos < len(buf):
        c = buf[pos]
        if 0x30 <= c <= 0x39 or c == 0x2d:
            break
        pos += 1
        n += 1
    return pos


def _read_int(buf, pos):
    """FUN_00507fc0: optional '-', then decimal digits; stops at the first other byte."""
    neg = pos < len(buf) and buf[pos] == 0x2d
    if neg:
        pos += 1
    v = 0
    while pos < len(buf) and 0x30 <= buf[pos] <= 0x39:
        v = v * 10 + buf[pos] - 0x30
        pos += 1
    return (-v if neg else v), pos


def _past(buf, pos, ch):
    """FUN_00507ca0: position just after the next `ch`, or None when there is none."""
    i = buf.find(ch, pos)
    return None if i < 0 else i + 1


def count_records(buf, start):
    """Number of `{` the game counts from `start` (it stops at 299, or when none is left)."""
    n, pos = 0, start
    for _ in range(MAX_RECORDS):
        pos = _past(buf, pos, b'{')
        if pos is None:
            break
        n += 1
    return n


def game_records(buf, marker, nfields, nrecords, per_record_lead=False):
    """Read `nrecords` records of `nfields` integers after the first `marker`."""
    start = buf.find(marker)
    if start < 0:
        raise ValueError('marker %r not found' % marker)
    pos = start
    out = []
    for _ in range(nrecords):
        pos = _past(buf, pos, b'{')
        if pos is None:
            raise ValueError('fewer than %d records after %r' % (nrecords, marker))
        rec = []
        for _ in range(nfields):
            pos = _skip_to_number(buf, pos)
            v, pos = _read_int(buf, pos)
            rec.append(v)
            nxt = _past(buf, pos, b',')
            if nxt is None:
                raise ValueError('record cut short after %r' % marker)
            pos = nxt
        out.append(rec)
    return out, start


def game_view(path):
    """What the game's loader would put in memory, as plain lists."""
    buf = open(path, 'rb').read()
    kind = kind_of(path)
    if kind == 'zeus_model':
        b_start = buf.find(b'ALL BUILDINGS')
        h_start = buf.find(b'ALL HOUSES')
        total = count_records(buf[b_start:], 0)
        houses = count_records(buf[h_start:], 0)
        buildings, _ = game_records(buf, b'ALL BUILDINGS', BUILDING_FIELDS, total - houses, 0)
        hs, _ = game_records(buf, b'ALL HOUSES', HOUSE_FIELDS, houses)
        return {'buildings': buildings, 'houses': hs}
    if kind == 'figure_model':
        total = count_records(buf[buf.find(b'ALL FIGURES'):], 0)
        enemies = count_records(buf[buf.find(b'ALL ENEMIES'):], 0)
        figs, _ = game_records(buf, b'ALL FIGURES', FIGURE_FIELDS, total - enemies)
        ens, _ = game_records(buf, b'ALL ENEMIES', FIGURE_FIELDS, enemies)
        return {'figures': figs, 'enemies': ens}
    if kind == 'tax':
        recs = []
        pos = 0
        for _ in range(count_records(buf, 0)):
            pos = _past(buf, pos, b'{')
            row = []
            for _ in range(TAX_COLUMNS):
                pos = _skip_to_number(buf, pos)
                v, pos = _read_int(buf, pos)
                row.append(v)
                pos = _past(buf, pos, b',') or pos
            recs.append(row)
        return {'tax': recs}
    if kind == 'monster':
        recs, _ = game_records(buf, b'', MONSTER_FIELDS, count_records(buf, 0))
        return {'monsters': recs}
    raise ValueError('not a model table: %s' % path)


# --------------------------------------------------------------------------- labelled view

_REC = re.compile(rb'\{([^}\r\n]*)\}?')  # house rows have no closing brace


def _fields(text):
    out = []
    for tok in text.split(b','):
        tok = tok.strip()
        if tok:
            out.append(tok)
    return out


def _num(tok):
    return float(tok) if b'.' in tok else int(tok)


def parse(path):
    """Labelled records, or raise ValueError describing the first line that does not fit."""
    buf = open(path, 'rb').read()
    kind = kind_of(path)
    section = None
    res = {}
    for ln, raw in enumerate(buf.split(b'\n'), 1):
        line = raw.rstrip(b'\r')
        s = line.strip()
        if s in (b'ALL BUILDINGS', b'ALL HOUSES', b'ALL FIGURES', b'ALL ENEMIES'):
            section = s.decode()
            res.setdefault(section, [])
            continue
        if kind in ('zeus_model', 'figure_model'):
            if section is None or b'{' not in line:
                continue
            if section is not None and s.startswith(b'End of model data'):
                section = None
                continue
        elif kind == 'tax':
            if b'{' not in line or section is not None:
                continue
            section = None
        m = _REC.search(line)
        if m is None:
            continue
        head = line[:m.start()].rstrip(b', \t').decode('latin-1')
        vals = [_num(t) for t in _fields(m.group(1))]
        if kind == 'monster':
            res.setdefault('MONSTERS', []).append((head, vals))
        elif kind == 'tax':
            res.setdefault('TAX', []).append((head, vals))
        else:
            res.setdefault(section, []).append((head, vals))
    return res


# --------------------------------------------------------------------------- checks

def check_file(path):
    """Return (summary, problems). Problems empty means the file was read to the end."""
    kind = kind_of(path)
    problems = []
    try:
        gv = game_view(path)
        lv = parse(path)
    except ValueError as e:
        return '', [str(e)]
    summary = {}
    if kind == 'zeus_model':
        pairs = [('buildings', 'ALL BUILDINGS', BUILDING_FIELDS),
                 ('houses', 'ALL HOUSES', HOUSE_FIELDS)]
    elif kind == 'figure_model':
        pairs = [('figures', 'ALL FIGURES', FIGURE_FIELDS),
                 ('enemies', 'ALL ENEMIES', FIGURE_FIELDS)]
    elif kind == 'tax':
        pairs = [('tax', 'TAX', TAX_COLUMNS)]
    else:
        pairs = [('monsters', 'MONSTERS', None)]
    for gname, lname, width in pairs:
        if kind == 'monster':
            rows = gv['monsters']
            labelled = lv.get('MONSTERS', [])
            summary['monsters'] = len(rows)
            if len(rows) != len(labelled) or any(
                    [int(x) for x in v[:MONSTER_FIELDS]] != r for (_, v), r in zip(labelled, rows)):
                problems.append('monster rows differ between the two readers')
            continue
        g = gv[gname]
        l = lv.get(lname, [])
        summary[gname] = len(g)
        if len(g) != len(l):
            problems.append('%s: game reads %d records, line reader %d' % (gname, len(g), len(l)))
            continue
        for i, ((head, lvals), gvals) in enumerate(zip(l, g)):
            if len(lvals) < len(gvals):
                problems.append('%s #%d: only %d values' % (gname, i, len(lvals)))
            elif [int(x) for x in lvals[:len(gvals)]] != gvals:
                problems.append('%s #%d: the two readers disagree' % (gname, i))
    return ', '.join('%s=%d' % kv for kv in summary.items()), problems


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
        print(kind_of(argv[2]), level_of(argv[2]), s, 'OK' if not p else p)
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
        lv = parse(argv[2])
        for sec, rows in lv.items():
            for i, (head, vals) in enumerate(rows):
                print('\t'.join([sec, str(i), head] + [str(v) for v in vals]))
        return 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
