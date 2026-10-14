// KERNEL32 : tas, mémoire virtuelle et jeux de caractères pour le programme émulé.

#include <cstring>

#include "win32/kernel32_state.h"

namespace zeus::win32::kernel32 {
namespace {

// Classe d'un caractère au sens de GetStringType (CT_CTYPE1), pour le jeu de caractères latin-1.
uint16_t ctype1(unsigned c) {
    uint16_t t = 0;
    if (c < 0x20 || c == 0x7F) t |= 0x20;                                   // C1_CNTRL
    if (c == ' ' || (c >= 9 && c <= 13)) t |= 0x08;                         // C1_SPACE
    if (c == ' ' || c == '\t') t |= 0x40;                                   // C1_BLANK
    if (c >= '0' && c <= '9') t |= 0x04 | 0x80;                             // C1_DIGIT, C1_XDIGIT
    if ((c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) t |= 0x80;
    const bool upper = (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7);
    const bool lower_case = (c >= 'a' && c <= 'z') || (c >= 0xDF && c <= 0xFF && c != 0xF7);
    if (upper) t |= 0x01 | 0x100;                                           // C1_UPPER, C1_ALPHA
    if (lower_case) t |= 0x02 | 0x100;                                      // C1_LOWER, C1_ALPHA
    if (c > 0x20 && c < 0x7F && !std::isalnum(static_cast<int>(c))) t |= 0x10;  // C1_PUNCT
    return t;
}

unsigned map_case(unsigned c, uint32_t flags) {
    if (flags & 0x100) {  // LCMAP_LOWERCASE
        if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7)) return c + 32;
    } else if (flags & 0x200) {  // LCMAP_UPPERCASE
        if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7)) return c - 32;
    }
    return c;
}

}  // namespace

void install_memory(Process& p, const std::shared_ptr<State>& st) {
    Cpu& cpu = p.cpu();
    auto def = [&](const char* name, uint32_t args, std::function<uint32_t()> fn) {
        p.def("kernel32.dll", name, args * 4, std::move(fn));
    };

    // --- tas et mémoire virtuelle ------------------------------------------------------
    def("HeapCreate", 3, [] { return 0x00110000u; });
    def("HeapDestroy", 1, [] { return 1u; });
    def("HeapAlloc", 3, [&p] { return p.alloc(p.arg(2), (p.arg(1) & 8) != 0); });  // HEAP_ZERO_MEMORY
    def("HeapFree", 3, [&p] {
        p.release(p.arg(2));
        return 1u;
    });
    def("HeapSize", 3, [&p] { return p.alloc_size(p.arg(2)); });
    def("HeapReAlloc", 4, [&p, &cpu] {
        const uint32_t old = p.arg(2), size = p.arg(3), old_size = p.alloc_size(old);
        const uint32_t fresh = p.alloc(size, (p.arg(1) & 8) != 0);
        if (!fresh) return 0u;
        std::vector<uint8_t> copy(std::min(old_size, size));
        if (!copy.empty()) {
            cpu.read(old, copy.data(), copy.size());
            cpu.write(fresh, copy.data(), copy.size());
        }
        p.release(old);
        return fresh;
    });
    def("VirtualAlloc", 4, [&p, &cpu, st] {
        const uint32_t address = p.arg(0), size = (p.arg(1) + 0xFFF) & ~0xFFFu;
        if (address) {
            // Validation d'une plage déjà réservée : la mémoire y est présente et à zéro.
            for (const auto& [base, length] : st->virtual_regions)
                if (address >= base && address + size <= base + length) return address & ~0xFFFu;
            return 0u;
        }
        // Les blocs du tas sont alignés sur 16 octets : on sur-alloue pour aligner sur la page.
        const uint32_t raw = p.alloc(size + 0x1000);
        if (!raw) return 0u;
        const uint32_t base = (raw + 0xFFF) & ~0xFFFu;
        st->virtual_regions[base] = size;
        cpu.fill(base, 0, size);
        return base;
    });
    def("VirtualFree", 3, [&p, &cpu, st] {
        const uint32_t address = p.arg(0), size = p.arg(1), type = p.arg(2);
        if (type & 0x4000) {  // MEM_DECOMMIT : une nouvelle validation doit retrouver des zéros
            if (size) cpu.fill(address, 0, size);
        }
        return 1u;  // MEM_RELEASE : la plage n'est pas rendue au tas, par simplicité
    });

    // --- jeux de caractères ------------------------------------------------------------
    def("GetACP", 0, [] { return 1252u; });
    def("GetOEMCP", 0, [] { return 850u; });
    def("GetCPInfo", 2, [&p, &cpu] {
        cpu.fill(p.arg(1), 0, 18);
        cpu.write_u32(p.arg(1), 1);         // un octet par caractère
        cpu.write_u8(p.arg(1) + 4, '?');
        return 1u;
    });
    def("GetStringTypeW", 4, [&p, &cpu] {
        if (p.arg(0) != 1) return 0u;  // seul CT_CTYPE1 est servi
        const uint32_t count = p.arg(2) == 0xFFFFFFFF ? 0 : p.arg(2);
        for (uint32_t i = 0; i < count; ++i)
            cpu.write_u16(p.arg(3) + 2 * i, ctype1(cpu.read_u16(p.arg(1) + 2 * i)));
        return 1u;
    });
    def("GetStringTypeA", 5, [&p, &cpu] {
        if (p.arg(1) != 1) return 0u;
        const uint32_t count = p.arg(3) == 0xFFFFFFFF ? 0 : p.arg(3);
        for (uint32_t i = 0; i < count; ++i)
            cpu.write_u16(p.arg(4) + 2 * i, ctype1(cpu.read_u8(p.arg(2) + i)));
        return 1u;
    });
    def("LCMapStringA", 6, [&p, &cpu] {
        const uint32_t flags = p.arg(1), src = p.arg(2), dst = p.arg(4), room = p.arg(5);
        uint32_t count = p.arg(3);
        if (count == 0xFFFFFFFF) count = static_cast<uint32_t>(cpu.read_string(src, 1 << 20).size()) + 1;
        if (room == 0) return count;
        if (room < count) return 0u;
        for (uint32_t i = 0; i < count; ++i)
            cpu.write_u8(dst + i, static_cast<uint8_t>(map_case(cpu.read_u8(src + i), flags)));
        return count;
    });
    def("LCMapStringW", 6, [&p, &cpu] {
        const uint32_t flags = p.arg(1), src = p.arg(2), dst = p.arg(4), room = p.arg(5);
        uint32_t count = p.arg(3);
        if (count == 0xFFFFFFFF) {
            count = 0;
            while (cpu.read_u16(src + 2 * count)) ++count;
            ++count;
        }
        if (room == 0) return count;
        if (room < count) return 0u;
        for (uint32_t i = 0; i < count; ++i)
            cpu.write_u16(dst + 2 * i, static_cast<uint16_t>(map_case(cpu.read_u16(src + 2 * i), flags)));
        return count;
    });
    def("MultiByteToWideChar", 6, [&p, &cpu] {
        const uint32_t src = p.arg(2), dst = p.arg(4), room = p.arg(5);
        uint32_t count = p.arg(3);
        if (count == 0xFFFFFFFF) count = static_cast<uint32_t>(cpu.read_string(src, 1 << 20).size()) + 1;
        if (room == 0) return count;
        if (room < count) return 0u;
        for (uint32_t i = 0; i < count; ++i) cpu.write_u16(dst + 2 * i, cpu.read_u8(src + i));
        return count;
    });
    def("WideCharToMultiByte", 8, [&p, &cpu] {
        const uint32_t src = p.arg(2), dst = p.arg(4), room = p.arg(5);
        uint32_t count = p.arg(3);
        if (count == 0xFFFFFFFF) {
            count = 0;
            while (cpu.read_u16(src + 2 * count)) ++count;
            ++count;
        }
        if (room == 0) return count;
        if (room < count) return 0u;
        for (uint32_t i = 0; i < count; ++i) {
            const uint16_t c = cpu.read_u16(src + 2 * i);
            cpu.write_u8(dst + i, c < 256 ? static_cast<uint8_t>(c) : '?');
        }
        return count;
    });
    def("CompareStringA", 6, [&p, &cpu] {
        auto text = [&](uint32_t address, uint32_t count) {
            std::string s = count == 0xFFFFFFFF ? cpu.read_string(address, 1 << 20) : std::string(count, '\0');
            if (count != 0xFFFFFFFF && count) cpu.read(address, s.data(), count);
            return s;
        };
        std::string a = text(p.arg(2), p.arg(3)), b = text(p.arg(4), p.arg(5));
        if (p.arg(1) & 1) {  // NORM_IGNORECASE
            a = lower(a);
            b = lower(b);
        }
        return a < b ? 1u : a == b ? 2u : 3u;
    });

    def("CompareStringW", 6, [&p, &cpu] {
        // Même comparaison, sur des caractères de 16 bits ; seule la casse ASCII est ignorée.
        auto text = [&](uint32_t address, uint32_t count) {
            std::u16string s;
            for (uint32_t i = 0; count == 0xFFFFFFFF ? i < (1u << 20) : i < count; ++i) {
                const char16_t c = cpu.read_u16(address + 2 * i);
                if (count == 0xFFFFFFFF && !c) break;
                s += (p.arg(1) & 1) && c >= u'A' && c <= u'Z' ? static_cast<char16_t>(c + 0x20) : c;
            }
            return s;
        };
        const std::u16string a = text(p.arg(2), p.arg(3)), b = text(p.arg(4), p.arg(5));
        return a < b ? 1u : a == b ? 2u : 3u;
    });
}

}  // namespace zeus::win32::kernel32
