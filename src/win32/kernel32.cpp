// KERNEL32 : mémoire, chaînes, temps, environnement et fichiers pour le programme émulé.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <thread>

#include "win32/process.h"
#include "win32/vfs.h"

namespace zeus::win32 {
namespace {

namespace fs = std::filesystem;

constexpr uint32_t kInvalidHandle = 0xFFFFFFFF;
constexpr uint32_t kErrorFileNotFound = 2, kErrorPathNotFound = 3, kErrorInvalidHandle = 6,
                   kErrorNoMoreFiles = 18, kErrorAlreadyExists = 183;

struct OpenFile {
    std::fstream stream;
    fs::path path;
};

struct FindState {
    std::vector<VfsEntry> entries;
    size_t next = 0;
};

struct State {
    explicit State(Process& p) : vfs(p.vfs()) {}
    Vfs& vfs;
    std::map<uint32_t, std::unique_ptr<OpenFile>> files;
    std::map<uint32_t, FindState> finds;
    std::map<uint32_t, uint32_t> virtual_regions;  // adresse -> taille
    std::map<std::string, std::string> environment;
    uint32_t next_handle = 0x100;
    uint32_t command_line = 0, environment_a = 0, environment_w = 0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

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

void write_systemtime(Cpu& cpu, uint32_t address, const std::tm& t, int ms) {
    const uint16_t fields[8] = {static_cast<uint16_t>(t.tm_year + 1900), static_cast<uint16_t>(t.tm_mon + 1),
                                static_cast<uint16_t>(t.tm_wday), static_cast<uint16_t>(t.tm_mday),
                                static_cast<uint16_t>(t.tm_hour), static_cast<uint16_t>(t.tm_min),
                                static_cast<uint16_t>(t.tm_sec), static_cast<uint16_t>(ms)};
    for (int i = 0; i < 8; ++i) cpu.write_u16(address + 2 * static_cast<uint32_t>(i), fields[i]);
}

void write_find_data(Cpu& cpu, uint32_t address, const VfsEntry& e) {
    cpu.fill(address, 0, 320);
    cpu.write_u32(address, e.is_directory ? 0x10 : 0x20);  // DIRECTORY ou ARCHIVE
    for (uint32_t offset : {4u, 12u, 20u}) {               // création, accès, écriture
        cpu.write_u32(address + offset, static_cast<uint32_t>(e.filetime));
        cpu.write_u32(address + offset + 4, static_cast<uint32_t>(e.filetime >> 32));
    }
    cpu.write_u32(address + 28, static_cast<uint32_t>(e.size >> 32));
    cpu.write_u32(address + 32, static_cast<uint32_t>(e.size));
    cpu.write_string(address + 44, e.name.substr(0, 259));
}

}  // namespace

void install_kernel32(Process& p) {
    auto st = std::make_shared<State>(p);
    Cpu& cpu = p.cpu();
    const char* dll = "kernel32.dll";
    auto def = [&](const char* name, uint32_t args, std::function<uint32_t()> fn) {
        p.def(dll, name, args * 4, std::move(fn));
    };
    auto ticks = [&p] { return p.ticks(); };

    // --- démarrage, versions, environnement --------------------------------------------
    def("GetVersion", 0, [] { return 0x0A280105u; });  // Windows XP, build 2600
    def("GetVersionExA", 1, [&p, &cpu] {
        const uint32_t info = p.arg(0);
        cpu.write_u32(info + 4, 5);      // version majeure
        cpu.write_u32(info + 8, 1);      // version mineure
        cpu.write_u32(info + 12, 2600);  // build
        cpu.write_u32(info + 16, 2);     // plateforme NT
        cpu.write_u8(info + 20, 0);
        return 1u;
    });
    def("GetCommandLineA", 0, [&p, st] {
        if (!st->command_line) st->command_line = p.alloc_string(std::string("\"") + kGuestDir + "\\Zeus.exe\"");
        return st->command_line;
    });
    def("GetStartupInfoA", 1, [&p, &cpu] {
        cpu.fill(p.arg(0), 0, 68);
        cpu.write_u32(p.arg(0), 68);
        return 0u;
    });
    def("GetModuleHandleA", 1, [&p] { return p.arg(0) == 0 ? p.image().image_base : 0u; });
    def("GetModuleFileNameA", 3, [&p, &cpu] {
        const std::string name = std::string(kGuestDir) + "\\Zeus.exe";
        const uint32_t size = p.arg(2);
        if (size == 0) return 0u;
        const std::string cut = name.substr(0, size - 1);
        cpu.write_string(p.arg(1), cut);
        return static_cast<uint32_t>(cut.size());
    });
    def("GetEnvironmentStrings", 0, [&p, st] {
        if (!st->environment_a) st->environment_a = p.alloc(4);  // bloc vide : deux octets nuls
        return st->environment_a;
    });
    def("GetEnvironmentStringsW", 0, [&p, st] {
        if (!st->environment_w) st->environment_w = p.alloc(8);
        return st->environment_w;
    });
    def("FreeEnvironmentStringsA", 1, [] { return 1u; });
    def("FreeEnvironmentStringsW", 1, [] { return 1u; });
    def("GetEnvironmentVariableA", 3, [&p, &cpu, st] {
        const auto found = st->environment.find(lower(cpu.read_string(p.arg(0))));
        if (found == st->environment.end()) return 0u;
        if (found->second.size() + 1 > p.arg(2)) return static_cast<uint32_t>(found->second.size() + 1);
        cpu.write_string(p.arg(1), found->second);
        return static_cast<uint32_t>(found->second.size());
    });
    def("SetEnvironmentVariableA", 2, [&p, &cpu, st] {
        const std::string name = lower(cpu.read_string(p.arg(0)));
        if (p.arg(1)) st->environment[name] = cpu.read_string(p.arg(1));
        else st->environment.erase(name);
        return 1u;
    });
    def("GetStdHandle", 1, [] { return 0u; });
    def("SetStdHandle", 2, [] { return 1u; });
    def("GetFileType", 1, [&p, st] { return st->files.count(p.arg(0)) ? 1u : 0u; });  // FILE_TYPE_DISK
    def("SetHandleCount", 1, [&p] { return p.arg(0); });
    def("GetLastError", 0, [&p] { return p.last_error; });
    def("GetCurrentProcess", 0, [] { return 0xFFFFFFFFu; });
    def("ExitProcess", 1, [&p] {
        p.exit(static_cast<int>(p.arg(0)));
        return 0u;
    });
    def("TerminateProcess", 2, [&p] {
        p.exit(static_cast<int>(p.arg(1)));
        return 1u;
    });
    def("SetUnhandledExceptionFilter", 1, [] { return 0u; });
    def("UnhandledExceptionFilter", 1, [] { return 0u; });  // EXCEPTION_CONTINUE_SEARCH
    def("IsBadReadPtr", 2, [&p] { return p.arg(0) == 0 ? 1u : 0u; });
    def("IsBadWritePtr", 2, [&p] { return p.arg(0) == 0 ? 1u : 0u; });
    def("IsBadCodePtr", 1, [&p] { return p.arg(0) == 0 ? 1u : 0u; });
    def("OutputDebugStringA", 1, [&p, &cpu] {
        std::fprintf(stderr, "[jeu] %s\n", cpu.read_string(p.arg(0)).c_str());
        return 0u;
    });
    def("LoadLibraryA", 1, [&p, &cpu] {
        std::fprintf(stderr, "[hôte] LoadLibraryA(%s) refusé\n", cpu.read_string(p.arg(0)).c_str());
        p.last_error = 126;  // ERROR_MOD_NOT_FOUND
        return 0u;
    });
    def("GetProcAddress", 2, [] { return 0u; });

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

    // --- temps -------------------------------------------------------------------------
    def("GetTickCount", 0, [ticks] { return ticks(); });
    def("QueryPerformanceFrequency", 1, [&p, &cpu] {
        cpu.write_u32(p.arg(0), 1000000);
        cpu.write_u32(p.arg(0) + 4, 0);
        return 1u;
    });
    def("QueryPerformanceCounter", 1, [&p, &cpu, st] {
        using namespace std::chrono;
        const uint64_t us = p.fixed_clock ? static_cast<uint64_t>(p.ticks()) * 1000
                                          : static_cast<uint64_t>(
                                                duration_cast<microseconds>(steady_clock::now() - st->start).count());
        cpu.write_u32(p.arg(0), static_cast<uint32_t>(us));
        cpu.write_u32(p.arg(0) + 4, static_cast<uint32_t>(us >> 32));
        return 1u;
    });
    def("Sleep", 1, [&p] {
        if (p.fixed_clock) {
            p.advance_clock(p.arg(0));
            return 0u;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min<uint32_t>(p.arg(0), 100)));
        return 0u;
    });
    auto system_time = [&p, &cpu](bool local) {
        using namespace std::chrono;
        // Horloge fictive : le 1er janvier 2001 à midi, plus le temps écoulé.
        const auto now = p.fixed_clock ? system_clock::from_time_t(978350400) + milliseconds(p.ticks()) : system_clock::now();
        const std::time_t seconds = system_clock::to_time_t(now);
        std::tm t{};
        if (local) localtime_r(&seconds, &t);
        else gmtime_r(&seconds, &t);
        const int ms = static_cast<int>(duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);
        write_systemtime(cpu, p.arg(0), t, ms);
        return 0u;
    };
    def("GetLocalTime", 1, [system_time] { return system_time(true); });
    def("GetSystemTime", 1, [system_time] { return system_time(false); });
    def("GetTimeZoneInformation", 1, [&p, &cpu] {
        cpu.fill(p.arg(0), 0, 172);
        return 0u;  // TIME_ZONE_ID_UNKNOWN, décalage nul
    });

    // --- fichiers ----------------------------------------------------------------------
    def("CreateFileA", 7, [&p, &cpu, st] {
        const std::string name = cpu.read_string(p.arg(0));
        const uint32_t access = p.arg(1), disposition = p.arg(4);
        if (p.trace) std::fprintf(stderr, "    fichier « %s » accès %#x mode %u\n", name.c_str(), access, disposition);
        const bool write = (access & 0x40000000) != 0;
        const auto existing = st->vfs.find(name);
        // 1 CREATE_NEW, 2 CREATE_ALWAYS, 3 OPEN_EXISTING, 4 OPEN_ALWAYS, 5 TRUNCATE_EXISTING
        if ((disposition == 3 || disposition == 5) && !existing) {
            p.last_error = kErrorFileNotFound;
            return kInvalidHandle;
        }
        if (disposition == 1 && existing) {
            p.last_error = kErrorAlreadyExists;
            return kInvalidHandle;
        }
        auto file = std::make_unique<OpenFile>();
        if (write || disposition == 2) {
            const bool truncate = disposition == 2 || disposition == 5 || disposition == 1;
            const auto target = st->vfs.writable(name, !truncate);
            if (!target) {
                p.last_error = kErrorPathNotFound;
                return kInvalidHandle;
            }
            file->path = *target;
            if (truncate || !fs::exists(*target)) std::ofstream(*target, std::ios::binary).close();
            file->stream.open(*target, std::ios::binary | std::ios::in | std::ios::out);
        } else {
            if (fs::is_directory(*existing)) {
                p.last_error = 5;  // ERROR_ACCESS_DENIED
                return kInvalidHandle;
            }
            file->path = *existing;
            file->stream.open(*existing, std::ios::binary | std::ios::in);
        }
        if (!file->stream) {
            p.last_error = kErrorFileNotFound;
            return kInvalidHandle;
        }
        p.last_error = 0;
        const uint32_t handle = st->next_handle += 4;
        st->files[handle] = std::move(file);
        return handle;
    });
    def("CloseHandle", 1, [&p, st] {
        st->files.erase(p.arg(0));
        return 1u;
    });
    def("ReadFile", 5, [&p, &cpu, st] {
        const auto found = st->files.find(p.arg(0));
        if (found == st->files.end()) {
            p.last_error = kErrorInvalidHandle;
            return 0u;
        }
        std::vector<char> buffer(p.arg(2));
        std::fstream& f = found->second->stream;
        f.clear();
        f.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const uint32_t got = static_cast<uint32_t>(f.gcount());
        f.clear();
        if (got) cpu.write(p.arg(1), buffer.data(), got);
        if (p.arg(3)) cpu.write_u32(p.arg(3), got);
        return 1u;
    });
    def("WriteFile", 5, [&p, &cpu, st] {
        const auto found = st->files.find(p.arg(0));
        if (found == st->files.end()) {
            p.last_error = kErrorInvalidHandle;
            return 0u;
        }
        std::vector<char> buffer(p.arg(2));
        if (!buffer.empty()) cpu.read(p.arg(1), buffer.data(), buffer.size());
        std::fstream& f = found->second->stream;
        f.clear();
        f.seekp(f.tellg());
        f.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        f.seekg(f.tellp());
        if (p.arg(3)) cpu.write_u32(p.arg(3), f ? static_cast<uint32_t>(buffer.size()) : 0);
        return f ? 1u : 0u;
    });
    def("SetFilePointer", 4, [&p, st] {
        const auto found = st->files.find(p.arg(0));
        if (found == st->files.end()) {
            p.last_error = kErrorInvalidHandle;
            return kInvalidHandle;
        }
        std::fstream& f = found->second->stream;
        f.clear();
        const auto way = p.arg(3) == 0 ? std::ios::beg : p.arg(3) == 1 ? std::ios::cur : std::ios::end;
        f.seekg(static_cast<int32_t>(p.arg(1)), way);
        if (!f) {
            f.clear();
            return kInvalidHandle;
        }
        return static_cast<uint32_t>(f.tellg());
    });
    def("GetFileSize", 2, [&p, &cpu, st] {
        const auto found = st->files.find(p.arg(0));
        if (found == st->files.end()) return kInvalidHandle;
        found->second->stream.flush();
        std::error_code ec;
        const uint64_t size = fs::file_size(found->second->path, ec);
        if (p.arg(1)) cpu.write_u32(p.arg(1), static_cast<uint32_t>(size >> 32));
        return static_cast<uint32_t>(size);
    });
    def("SetEndOfFile", 1, [&p, st] {
        const auto found = st->files.find(p.arg(0));
        if (found == st->files.end()) return 0u;
        std::fstream& f = found->second->stream;
        f.flush();
        std::error_code ec;
        fs::resize_file(found->second->path, static_cast<uintmax_t>(f.tellg()), ec);
        return ec ? 0u : 1u;
    });
    def("FlushFileBuffers", 1, [&p, st] {
        const auto found = st->files.find(p.arg(0));
        if (found != st->files.end()) found->second->stream.flush();
        return 1u;
    });
    def("GetFileInformationByHandle", 2, [&p, &cpu, st] {
        const auto found = st->files.find(p.arg(0));
        if (found == st->files.end()) return 0u;
        found->second->stream.flush();
        std::error_code ec;
        const uint64_t size = fs::file_size(found->second->path, ec);
        const uint64_t time = to_filetime(fs::last_write_time(found->second->path, ec));
        const uint32_t info = p.arg(1);
        cpu.fill(info, 0, 52);
        cpu.write_u32(info, 0x20);
        for (uint32_t offset : {4u, 12u, 20u}) {
            cpu.write_u32(info + offset, static_cast<uint32_t>(time));
            cpu.write_u32(info + offset + 4, static_cast<uint32_t>(time >> 32));
        }
        cpu.write_u32(info + 32, static_cast<uint32_t>(size >> 32));
        cpu.write_u32(info + 36, static_cast<uint32_t>(size));
        cpu.write_u32(info + 40, 1);
        return 1u;
    });
    def("GetFileAttributesA", 1, [&p, &cpu, st] {
        const auto found = st->vfs.find(cpu.read_string(p.arg(0)));
        if (!found) {
            p.last_error = kErrorFileNotFound;
            return kInvalidHandle;
        }
        return fs::is_directory(*found) ? 0x10u : 0x20u;
    });
    def("SetFileAttributesA", 2, [] { return 1u; });
    def("DeleteFileA", 1, [&p, &cpu, st] {
        if (st->vfs.remove(cpu.read_string(p.arg(0)))) return 1u;
        p.last_error = kErrorFileNotFound;
        return 0u;
    });
    def("CopyFileA", 3, [&p, &cpu, st] {
        const auto source = st->vfs.find(cpu.read_string(p.arg(0)));
        const std::string target_name = cpu.read_string(p.arg(1));
        if (!source || (p.arg(2) && st->vfs.find(target_name))) {
            p.last_error = source ? kErrorAlreadyExists : kErrorFileNotFound;
            return 0u;
        }
        const auto target = st->vfs.writable(target_name, false);
        std::error_code ec;
        if (!target || !fs::copy_file(*source, *target, fs::copy_options::overwrite_existing, ec)) return 0u;
        return 1u;
    });
    def("CreateDirectoryA", 2, [&p, &cpu, st] {
        if (st->vfs.make_directory(cpu.read_string(p.arg(0)))) return 1u;
        p.last_error = kErrorAlreadyExists;
        return 0u;
    });
    def("RemoveDirectoryA", 1, [&p, &cpu, st] { return st->vfs.remove(cpu.read_string(p.arg(0))) ? 1u : 0u; });
    def("GetCurrentDirectoryA", 2, [&p, &cpu, st] {
        const std::string& cwd = st->vfs.current_directory();
        if (cwd.size() + 1 > p.arg(0)) return static_cast<uint32_t>(cwd.size() + 1);
        cpu.write_string(p.arg(1), cwd);
        return static_cast<uint32_t>(cwd.size());
    });
    def("SetCurrentDirectoryA", 1, [&p, &cpu, st] {
        return st->vfs.set_current_directory(cpu.read_string(p.arg(0))) ? 1u : 0u;
    });
    def("GetFullPathNameA", 4, [&p, &cpu, st] {
        const std::string full = st->vfs.absolute(cpu.read_string(p.arg(0)));
        if (full.size() + 1 > p.arg(1)) return static_cast<uint32_t>(full.size() + 1);
        cpu.write_string(p.arg(2), full);
        if (p.arg(3)) cpu.write_u32(p.arg(3), p.arg(2) + static_cast<uint32_t>(full.find_last_of('\\') + 1));
        return static_cast<uint32_t>(full.size());
    });
    def("FindFirstFileA", 2, [&p, &cpu, st] {
        FindState find;
        find.entries = st->vfs.list(cpu.read_string(p.arg(0)));
        if (find.entries.empty()) {
            p.last_error = kErrorFileNotFound;
            return kInvalidHandle;
        }
        write_find_data(cpu, p.arg(1), find.entries[0]);
        find.next = 1;
        const uint32_t handle = st->next_handle += 4;
        st->finds[handle] = std::move(find);
        return handle;
    });
    def("FindNextFileA", 2, [&p, &cpu, st] {
        const auto found = st->finds.find(p.arg(0));
        if (found == st->finds.end() || found->second.next >= found->second.entries.size()) {
            p.last_error = kErrorNoMoreFiles;
            return 0u;
        }
        write_find_data(cpu, p.arg(1), found->second.entries[found->second.next++]);
        return 1u;
    });
    def("FindClose", 1, [&p, st] {
        st->finds.erase(p.arg(0));
        return 1u;
    });
    def("CompareFileTime", 2, [&p, &cpu] {
        const uint64_t a = cpu.read_u32(p.arg(0)) | static_cast<uint64_t>(cpu.read_u32(p.arg(0) + 4)) << 32;
        const uint64_t b = cpu.read_u32(p.arg(1)) | static_cast<uint64_t>(cpu.read_u32(p.arg(1) + 4)) << 32;
        return a < b ? 0xFFFFFFFFu : a == b ? 0u : 1u;
    });
    def("GetDriveTypeA", 1, [&p, &cpu] {
        const std::string root = p.arg(0) ? cpu.read_string(p.arg(0)) : std::string(kGuestDir);
        return !root.empty() && std::toupper(static_cast<unsigned char>(root[0])) == 'C' ? 3u : 1u;  // disque fixe, sinon absent
    });
    def("GetDiskFreeSpaceA", 5, [&p, &cpu] {
        const uint32_t values[4] = {8, 512, 2000000, 4000000};  // secteurs/cluster, octets/secteur, libres, total
        for (int i = 0; i < 4; ++i)
            if (p.arg(1 + i)) cpu.write_u32(p.arg(1 + i), values[i]);
        return 1u;
    });
    def("GetVolumeInformationA", 8, [&p, &cpu] {
        if (p.arg(1) && p.arg(2)) cpu.write_string(p.arg(1), "");
        if (p.arg(3)) cpu.write_u32(p.arg(3), 0x12345678);
        if (p.arg(4)) cpu.write_u32(p.arg(4), 255);
        if (p.arg(5)) cpu.write_u32(p.arg(5), 0);
        if (p.arg(6) && p.arg(7)) cpu.write_string(p.arg(6), "NTFS");
        return 1u;
    });
    def("GetPrivateProfileStringA", 6, [&p, &cpu, st] {
        const std::string section = p.arg(0) ? lower(cpu.read_string(p.arg(0))) : "";
        const std::string key = p.arg(1) ? lower(cpu.read_string(p.arg(1))) : "";
        std::string value = p.arg(2) ? cpu.read_string(p.arg(2)) : "";
        const uint32_t room = p.arg(4);
        if (const auto path = st->vfs.find(cpu.read_string(p.arg(5)))) {
            std::ifstream in(*path);
            std::string line, current;
            while (std::getline(in, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                if (line.size() > 2 && line.front() == '[' && line.back() == ']') {
                    current = lower(line.substr(1, line.size() - 2));
                } else if (current == section) {
                    const size_t eq = line.find('=');
                    if (eq != std::string::npos && lower(line.substr(0, eq)) == key) {
                        value = line.substr(eq + 1);
                        break;
                    }
                }
            }
        }
        if (room == 0) return 0u;
        value = value.substr(0, room - 1);
        cpu.write_string(p.arg(3), value);
        return static_cast<uint32_t>(value.size());
    });
}

}  // namespace zeus::win32
