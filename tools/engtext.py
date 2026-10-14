#!/usr/bin/env python3
"""Reader for the game's text files (`Zeus_Text.eng`, `Zeus_Editor_Text.eng`, `Zeus_MM.eng`).

Format description: formats/eng.md. Standard library only.

Usage:
    engtext.py info  FILE            header, table and string statistics
    engtext.py check DIR             read every *.eng under DIR, rebuild each file byte for byte
    engtext.py dump  FILE GROUP      print the strings of one group (text file) or one record (MM file)

`check` prints one line per file and exits non-zero if any text file fails to parse, to
re-serialize identically, or to agree with the game's own string lookup. Files named `.eng`
that do not start with a known signature (the patch fragments of the updates) are listed as
skipped. `dump` prints game text: use it locally, never commit its output.
"""
import os
import struct
import sys

TEXT_MAGIC = b"Zeus textfile.\0\0"
MM_MAGIC = b"Zeus MM file.\0\0\0"

TEXT_TABLE_OFFSET = 0x1C
TEXT_TABLE_ENTRIES = 1000
TEXT_BLOB_OFFSET = TEXT_TABLE_OFFSET + 8 * TEXT_TABLE_ENTRIES  # 0x1F5C

MM_RECORDS_OFFSET = 0x18
MM_RECORD_SIZE = 0x50
MM_RECORD_CAPACITY = 1000
MM_BLOB_OFFSET = MM_RECORDS_OFFSET + MM_RECORD_SIZE * MM_RECORD_CAPACITY  # 0x13898
MM_TEXT_FIELDS = (17, 18, 19)  # u32 indexes inside a record: offsets of the three strings
MM_BLOB_LEAD = 16


class FormatError(Exception):
    pass


def split_strings(blob, start, count):
    """`count` NUL-terminated strings from `start`; returns (strings, end offset)."""
    out = []
    pos = start
    for _ in range(count):
        end = blob.find(b"\0", pos)
        if end < 0:
            raise FormatError("string runs past the end of the data at %d" % pos)
        out.append(blob[pos:end])
        pos = end + 1
    return out, pos


def game_string(blob, group_offset, index):
    """The game's own lookup (FUN_00507bf0 in the GOG executable), byte for byte.

    Walks from the group start; a NUL ends a string only if the byte before it is a printable
    byte (>= 0x20) or another NUL; after `index` ends, skips bytes below 0x20 and returns the
    string that starts there."""
    pos = group_offset
    while index > 0:
        if blob[pos] == 0 and (blob[pos - 1] >= 0x20 or blob[pos - 1] == 0):
            index -= 1
        pos += 1
    while blob[pos] < 0x20:
        pos += 1
    end = blob.index(b"\0", pos)
    return blob[pos:end]


# --- Zeus_Text.eng, Zeus_Editor_Text.eng ----------------------------------------------------

class TextFile:
    def __init__(self, data):
        if len(data) < TEXT_BLOB_OFFSET:
            raise FormatError("shorter than the header and table (%d bytes)" % len(data))
        if data[:16] != TEXT_MAGIC:
            raise FormatError("bad signature")
        self.group_count, self.string_total, self.header_extra = struct.unpack_from("<3I", data, 16)
        self.table = [struct.unpack_from("<2I", data, TEXT_TABLE_OFFSET + 8 * i)
                      for i in range(TEXT_TABLE_ENTRIES)]  # (offset, count)
        self.blob = data[TEXT_BLOB_OFFSET:]
        self._parse()

    def _parse(self):
        n = self.group_count
        if not 1 < n <= TEXT_TABLE_ENTRIES:
            raise FormatError("group count %d out of range" % n)
        if self.table[0] != (0, 0):
            raise FormatError("table entry 0 is not empty")
        if any(e != (0, 0) for e in self.table[n:]):
            raise FormatError("table entries after the last group are not empty")
        if sum(c for _, c in self.table) != self.string_total:
            raise FormatError("string total in the header does not match the table")
        self.groups = {}     # group number -> list of strings
        self.gaps = []       # (offset, bytes) of dead bytes between two groups
        self.lookup = {}     # group number -> offset the game lookup starts from
        last_offset = 0
        prev_end = 0
        for g in range(1, n):
            offset, count = self.table[g]
            if offset < last_offset:
                raise FormatError("group %d: offsets not increasing" % g)
            last_offset = offset
            self.lookup[g] = offset
            if count == 0:
                continue
            if offset < prev_end:
                raise FormatError("group %d overlaps the previous one" % g)
            if offset > prev_end:
                self.gaps.append((prev_end, self.blob[prev_end:offset]))
            strings, prev_end = split_strings(self.blob, offset, count)
            self.groups[g] = strings
        self.tail = self.blob[prev_end:]
        if any(self.tail):
            raise FormatError("non-zero bytes after the last group")
        # the game's lookup must give the same strings as the plain NUL-split
        for g, strings in self.groups.items():
            for i, s in enumerate(strings):
                if s == b"":
                    continue  # the game skips empty strings, see formats/eng.md
                if game_string(self.blob, self.lookup[g], i) != s:
                    raise FormatError("group %d string %d: game lookup differs" % (g, i))

    def rebuild(self):
        """Re-serialize from the parsed groups, gaps and tail."""
        n = self.group_count
        gaps = dict(self.gaps)
        blob = bytearray()
        table = [(0, 0)] * TEXT_TABLE_ENTRIES
        for g in range(1, n):
            offset, count = self.table[g]
            if count == 0:
                table[g] = (offset, 0)  # copy of the next group's start, checked at parse time
                continue
            blob += gaps.get(len(blob), b"")
            table[g] = (len(blob), count)
            for s in self.groups[g]:
                blob += s + b"\0"
        blob += self.tail
        head = TEXT_MAGIC + struct.pack("<3I", n, sum(c for _, c in table), self.header_extra)
        body = b"".join(struct.pack("<2I", o, c) for o, c in table)
        return head + body + bytes(blob)

    def info(self):
        used = [g for g in self.groups]
        words = sum(len(s.split()) for ss in self.groups.values() for s in ss)
        lines = [
            "text file: %d table entries (groups 1..%d), %d with strings, %d strings"
            % (self.group_count, self.group_count - 1, len(used), self.string_total),
            "header word 2: %d (%d words counted in the strings)" % (self.header_extra, words),
            "text area: %d bytes, %d dead bytes between groups, %d trailing NUL"
            % (len(self.blob), sum(len(b) for _, b in self.gaps), len(self.tail)),
            "empty groups: %s" % [g for g in range(1, self.group_count) if g not in self.groups],
            "largest group: %d strings (group %d)"
            % max((len(s), g) for g, s in self.groups.items()),
        ]
        return "\n".join(lines)

    def dump(self, group):
        if group not in self.groups:
            raise FormatError("group %d has no strings" % group)
        return ["%d.%d\t%s" % (group, i, show(s)) for i, s in enumerate(self.groups[group])]


# --- Zeus_MM.eng ------------------------------------------------------------------------------

class MMFile:
    def __init__(self, data):
        if len(data) < MM_BLOB_OFFSET:
            raise FormatError("shorter than the header and records (%d bytes)" % len(data))
        if data[:16] != MM_MAGIC:
            raise FormatError("bad signature")
        self.capacity, self.used = struct.unpack_from("<2I", data, 16)
        if self.capacity != MM_RECORD_CAPACITY or not 0 < self.used <= self.capacity:
            raise FormatError("record counts %d/%d out of range" % (self.used, self.capacity))
        self.records = [struct.unpack_from("<20I", data, MM_RECORDS_OFFSET + MM_RECORD_SIZE * r)
                        for r in range(self.capacity)]
        self.blob = data[MM_BLOB_OFFSET:]
        self._parse()

    def _parse(self):
        if any(any(r) for r in self.records[self.used:]):
            raise FormatError("records after the used ones are not empty")
        if not all(any(r) for r in self.records[:self.used]):
            raise FormatError("an empty record among the used ones")
        if self.blob[:MM_BLOB_LEAD] != bytes(MM_BLOB_LEAD):
            raise FormatError("text area does not start with 16 NUL bytes")
        refs = sorted(set(r[k] for r in self.records[:self.used] for k in MM_TEXT_FIELDS if r[k]))
        pos = MM_BLOB_LEAD
        self.strings = {}  # offset -> bytes
        for off in refs:
            if off != pos:
                raise FormatError("text area not contiguous: string expected at %d, found %d"
                                  % (pos, off))
            end = self.blob.find(b"\0", pos)
            if end < 0:
                raise FormatError("unterminated string at %d" % pos)
            self.strings[off] = self.blob[pos:end]
            pos = end + 1
        self.tail = self.blob[pos:]
        if any(self.tail):
            raise FormatError("non-zero bytes after the last string")
        n_refs = sum(1 for r in self.records[:self.used] for k in MM_TEXT_FIELDS if r[k])
        if n_refs != len(refs):
            raise FormatError("two records share a string")

    def text(self, record, field):
        off = self.records[record][field]
        return self.strings[off] if off else None

    def rebuild(self):
        blob = bytearray(MM_BLOB_LEAD)
        # strings are stored in offset order, which is record order
        for off in sorted(self.strings):
            if off != len(blob):
                raise FormatError("rebuild: offsets are not contiguous")
            blob += self.strings[off] + b"\0"
        blob += self.tail
        head = MM_MAGIC + struct.pack("<2I", self.capacity, self.used)
        body = b"".join(struct.pack("<20I", *r) for r in self.records)
        return head + body + bytes(blob)

    def info(self):
        kinds = {}
        for r in self.records[:self.used]:
            kinds[r[0] >> 16] = kinds.get(r[0] >> 16, 0) + 1
        codes = {}
        for s in self.strings.values():
            i = s.find(b"@")
            while i >= 0:
                c = s[i + 1:i + 2]
                key = "@" + (c.decode("latin-1") if not c.isdigit() else "<n>")
                codes[key] = codes.get(key, 0) + 1
                i = s.find(b"@", i + 1)
        return "\n".join([
            "MM file: %d records used of %d, %d strings, text area %d bytes (%d trailing NUL)"
            % (self.used, self.capacity, len(self.strings), len(self.blob), len(self.tail)),
            "record type (u16 at +2) counts: %s" % dict(sorted(kinds.items())),
            "@ codes: %s" % dict(sorted(codes.items())),
        ])

    def dump(self, record):
        if not 0 <= record < self.used:
            raise FormatError("record %d out of range (0..%d)" % (record, self.used - 1))
        r = self.records[record]
        out = ["record %d: u16 +0 = %d, +2 = %d" % (record, r[0] & 0xFFFF, r[0] >> 16),
               "fields: " + " ".join("%d:%#x" % (i, v) for i, v in enumerate(r) if v and i not in MM_TEXT_FIELDS)]
        for name, k in (("title", 17), ("caption", 18), ("body", 19)):
            t = self.text(record, k)
            out.append("%s\t%s" % (name, "-" if t is None else show(t)))
        return out


# --- common -------------------------------------------------------------------------------------

def show(raw):
    """Windows-1252 text, control characters escaped."""
    text = raw.decode("cp1252", errors="replace")
    return "".join(c if c >= " " else "\\x%02x" % ord(c) for c in text)


def load(data):
    if data[:16] == TEXT_MAGIC:
        return TextFile(data)
    if data[:16] == MM_MAGIC:
        return MMFile(data)
    return None


def cmd_info(path):
    with open(path, "rb") as f:
        data = f.read()
    obj = load(data)
    if obj is None:
        raise FormatError("not a text file")
    print("%s: %d bytes" % (path, len(data)))
    print(obj.info())


def cmd_check(folder):
    found = []
    for base, _, names in os.walk(folder):
        for name in sorted(names):
            if name.lower().endswith(".eng"):
                found.append(os.path.join(base, name))
    found.sort()
    ok = bad = skipped = 0
    for path in found:
        with open(path, "rb") as f:
            data = f.read()
        try:
            obj = load(data)
            if obj is None:
                skipped += 1
                print("skip %s (no known signature)" % path)
                continue
            if obj.rebuild() != data:
                raise FormatError("rebuilt file differs from the original")
            ok += 1
            kind = "text" if isinstance(obj, TextFile) else "MM  "
            print("ok   %s  %s  %d bytes" % (kind, path, len(data)))
        except (FormatError, struct.error, IndexError, ValueError) as e:
            bad += 1
            print("FAIL %s: %s" % (path, e))
    print("%d ok, %d failed, %d skipped" % (ok, bad, skipped))
    return 1 if bad or not ok else 0


def cmd_dump(path, number):
    with open(path, "rb") as f:
        obj = load(f.read())
    if obj is None:
        raise FormatError("not a text file")
    for line in obj.dump(number):
        print(line)


def main(argv):
    try:
        if len(argv) == 3 and argv[1] == "info":
            return cmd_info(argv[2]) or 0
        if len(argv) == 3 and argv[1] == "check":
            return cmd_check(argv[2])
        if len(argv) == 4 and argv[1] == "dump":
            return cmd_dump(argv[2], int(argv[3])) or 0
    except (FormatError, OSError, ValueError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
