// Hôte du code d'origine du jeu : charge Zeus.exe dans un processeur émulé cloisonné.
//
//   zeus-run --selftest        vérifie le processeur émulé et l'appel de fonctions hôtes
//   zeus-run --inspect EXE     charge l'exécutable sans rien exécuter et liste ses imports

#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>

#include "win32/cpu.h"
#include "win32/pe_image.h"

using zeus::win32::Cpu;

namespace {

int selftest() {
    Cpu cpu;
    cpu.map(0x00001000, 0x4000);  // code en 0x1000, données en 0x2000, pile en 0x4000
    const uint32_t add = cpu.add_host_function(
        {"test_add", 8, [](Cpu& c) { return c.arg(0) + c.arg(1); }});
    cpu.write_u32(0x3000, add);  // case d'import
    const uint8_t code[] = {
        0xB8, 0x05, 0x00, 0x00, 0x00,        // mov eax, 5
        0x83, 0xC0, 0x07,                    // add eax, 7
        0x6B, 0xC0, 0x03,                    // imul eax, eax, 3      -> 36
        0xA3, 0x00, 0x20, 0x00, 0x00,        // mov [0x2000], eax
        0xD9, 0xE8,                          // fld1
        0xD9, 0xE8,                          // fld1
        0xDE, 0xC1,                          // faddp                 -> 2.0
        0xDB, 0x1D, 0x04, 0x20, 0x00, 0x00,  // fistp dword [0x2004]
        0x6A, 0x02,                          // push 2
        0x6A, 0x03,                          // push 3
        0xFF, 0x15, 0x00, 0x30, 0x00, 0x00,  // call [0x3000]         -> fonction hôte, stdcall
        0xA3, 0x08, 0x20, 0x00, 0x00,        // mov [0x2008], eax
        0x89, 0x25, 0x0C, 0x20, 0x00, 0x00,  // mov [0x200c], esp
    };
    cpu.write(0x1000, code, sizeof code);
    cpu.set_reg(Cpu::Reg::ESP, 0x4800);
    cpu.run(0x1000, 0x1000 + sizeof code);
    const uint32_t a = cpu.read_u32(0x2000), b = cpu.read_u32(0x2004), c = cpu.read_u32(0x2008),
                   esp = cpu.read_u32(0x200C);
    std::printf("entiers : %u (attendu 36)\nflottants x87 : %u (attendu 2)\n"
                "fonction hôte : %u (attendu 5)\npile après l'appel : %#x (attendu 0x4800)\n",
                a, b, c, esp);
    const bool ok = a == 36 && b == 2 && c == 5 && esp == 0x4800;
    std::printf("%s\n", ok ? "autotest réussi" : "AUTOTEST EN ÉCHEC");
    return ok ? 0 : 1;
}

int inspect(const char* path) {
    Cpu cpu;
    const zeus::win32::PeImage image = zeus::win32::load_pe(path, cpu);
    std::printf("base %#x, point d'entrée %#x, taille en mémoire %u octets\n", image.image_base,
                image.entry_point, image.image_size);
    for (const auto& s : image.sections)
        std::printf("  section %-8s %#010x  %8u octets en mémoire, %8u dans le fichier\n",
                    s.name.c_str(), s.address, s.virtual_size, s.raw_size);
    std::map<std::string, int> per_dll;
    for (const auto& i : image.imports) ++per_dll[i.dll];
    std::printf("%zu fonctions importées :\n", image.imports.size());
    for (const auto& [dll, count] : per_dll) std::printf("  %-14s %d\n", dll.c_str(), count);
    for (const auto& s : image.sections)
        if (image.entry_point >= s.address && image.entry_point < s.address + s.virtual_size)
            std::printf("le point d'entrée est dans la section %s\n", s.name.c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && !std::strcmp(argv[1], "--selftest")) return selftest();
        if (argc == 3 && !std::strcmp(argv[1], "--inspect")) return inspect(argv[2]);
        std::fprintf(stderr, "usage : zeus-run --selftest | --inspect EXE\n");
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "zeus-run : %s\n", e.what());
        return 1;
    }
}
