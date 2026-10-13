#!/usr/bin/env python3
"""Premier examen d'un exécutable PE 32 bits : identité, sections, imports, indices de protection.

Usage : pe_triage.py FICHIER.EXE [--imports] [--strings MOTIF]
"""
import hashlib
import math
import re
import struct
import sys
import time


def entropy(data):
    if not data:
        return 0.0
    counts = [0] * 256
    for b in data:
        counts[b] += 1
    n = len(data)
    return -sum(c / n * math.log2(c / n) for c in counts if c)


class PE:
    def __init__(self, path):
        self.d = d = open(path, "rb").read()
        if d[:2] != b"MZ":
            raise ValueError("pas un exécutable MZ")
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        if d[pe:pe + 4] != b"PE\0\0":
            raise ValueError("pas un exécutable PE")
        self.machine, nsec, self.timestamp = struct.unpack_from("<HHI", d, pe + 4)
        opt_size = struct.unpack_from("<H", d, pe + 20)[0]
        opt = pe + 24
        self.magic = struct.unpack_from("<H", d, opt)[0]
        self.linker = d[opt + 2], d[opt + 3]
        self.entry = struct.unpack_from("<I", d, opt + 16)[0]
        self.image_base = struct.unpack_from("<I", d, opt + 28)[0]
        self.subsystem = struct.unpack_from("<H", d, opt + 68)[0]
        ndirs = struct.unpack_from("<I", d, opt + 92)[0]
        self.dirs = [struct.unpack_from("<2I", d, opt + 96 + 8 * i) for i in range(ndirs)]
        self.sections = []
        for i in range(nsec):
            s = opt + opt_size + 40 * i
            name = d[s:s + 8].rstrip(b"\0").decode("latin-1")
            vsize, va, rsize, rptr = struct.unpack_from("<4I", d, s + 8)
            flags = struct.unpack_from("<I", d, s + 36)[0]
            self.sections.append((name, va, vsize, rptr, rsize, flags))
        self.overlay = max(rptr + rsize for _, _, _, rptr, rsize, _ in self.sections)

    def offset(self, rva):
        for _, va, vsize, rptr, rsize, _ in self.sections:
            if va <= rva < va + max(vsize, rsize):
                return rva - va + rptr
        raise ValueError(f"RVA {rva:#x} hors sections")

    def cstr(self, rva):
        o = self.offset(rva)
        return self.d[o:self.d.index(b"\0", o)].decode("latin-1")

    def imports(self):
        rva, size = self.dirs[1]
        result = {}
        if not rva:
            return result
        o = self.offset(rva)
        while True:
            ilt, _, _, name, iat = struct.unpack_from("<5I", self.d, o)
            if not name:
                break
            funcs = []
            t = self.offset(ilt or iat)
            while True:
                v = struct.unpack_from("<I", self.d, t)[0]
                if not v:
                    break
                funcs.append(f"#{v & 0xFFFF}" if v & 0x80000000 else self.cstr(v + 2))
                t += 4
            result.setdefault(self.cstr(name).upper(), []).extend(funcs)
            o += 20
        return result


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    path = argv[1]
    pe = PE(path)
    d = pe.d
    print(f"fichier      : {path}")
    print(f"taille       : {len(d)} octets")
    print(f"sha256       : {hashlib.sha256(d).hexdigest()}")
    print(f"machine      : {pe.machine:#x} ({'i386' if pe.machine == 0x14C else '?'}), "
          f"PE32{'+' if pe.magic == 0x20B else ''}, sous-système {pe.subsystem}")
    print(f"compilé le   : {time.strftime('%Y-%m-%d %H:%M:%S', time.gmtime(pe.timestamp))} UTC, "
          f"éditeur de liens {pe.linker[0]}.{pe.linker[1]}")
    print(f"image base   : {pe.image_base:#x}, point d'entrée {pe.image_base + pe.entry:#x}")
    print("sections     :")
    for name, va, vsize, rptr, rsize, flags in pe.sections:
        e = entropy(d[rptr:rptr + rsize])
        rights = "".join(c for c, bit in (("R", 0x40000000), ("W", 0x80000000), ("X", 0x20000000))
                         if flags & bit)
        print(f"   {name:8} va={pe.image_base + va:#010x} vsize={vsize:>8} raw={rsize:>8} "
              f"entropie={e:4.2f} {rights}")
    print(f"données après le PE : {len(d) - pe.overlay} octets")
    imports = pe.imports()
    print("imports      : " + ", ".join(f"{k} ({len(v)})" for k, v in sorted(imports.items())))
    marks = [m for m in (b"BoG_ *90.0&!!  Yy>", b"stxt774", b"stxt371", b".cms_t", b"AddD\x03\x00\x00\x00", b"SafeDisc",
                         b"SecuROM", b"~0017.tmp", b"CLCD32", b"UPX!", b"ASPack", b"PECompact",
                         b"LaserLock", b"\x00.petite") if m in d]
    print(f"marqueurs de protection/packer connus : {marks or 'aucun'}")
    if "--imports" in argv:
        for dll, funcs in sorted(imports.items()):
            print(f"\n[{dll}] " + " ".join(funcs))
    if "--strings" in argv:
        pattern = re.compile(argv[argv.index("--strings") + 1].encode("latin-1"), re.I)
        for m in re.finditer(rb"[\x20-\x7e\x80-\xff]{5,}", d):
            if pattern.search(m.group()):
                print(f"   {m.start():#09x}  {m.group().decode('latin-1')}")


if __name__ == "__main__":
    main(sys.argv)
