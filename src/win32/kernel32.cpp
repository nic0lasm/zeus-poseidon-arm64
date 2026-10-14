// KERNEL32 : démarrage, environnement et temps pour le programme émulé ; la mémoire et les
// chaînes sont dans kernel32_memory.cpp, les fichiers dans kernel32_files.cpp.

#include <chrono>
#include <cstdio>
#include <ctime>
#include <thread>

#include "win32/kernel32_state.h"

namespace zeus::win32 {
namespace {

using namespace kernel32;

void write_systemtime(Cpu& cpu, uint32_t address, const std::tm& t, int ms) {
    const uint16_t fields[8] = {static_cast<uint16_t>(t.tm_year + 1900), static_cast<uint16_t>(t.tm_mon + 1),
                                static_cast<uint16_t>(t.tm_wday), static_cast<uint16_t>(t.tm_mday),
                                static_cast<uint16_t>(t.tm_hour), static_cast<uint16_t>(t.tm_min),
                                static_cast<uint16_t>(t.tm_sec), static_cast<uint16_t>(ms)};
    for (int i = 0; i < 8; ++i) cpu.write_u16(address + 2 * static_cast<uint32_t>(i), fields[i]);
}

}  // namespace

void install_kernel32(Process& p) {
    auto st = std::make_shared<kernel32::State>(p);
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

    kernel32::install_memory(p, st);

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

    kernel32::install_files(p, st);
}

}  // namespace zeus::win32
