// Diagnostics de zeus-run : autotest et banc du processeur émulé, inspection d'un exécutable,
// vérification des sons.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "tools/zeus_run/zeus_run.h"
#include "win32/audio.h"
#include "win32/cpu.h"
#include "win32/pe_image.h"

namespace zeus::run {

using zeus::win32::Cpu;

int selftest() {
    Cpu cpu;
    cpu.map(0x00001000, 0x1000, true);
    cpu.map(0x00002000, 0x3000);  // code en 0x1000, données en 0x2000, pile en 0x4000
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
        0x64, 0xA1, 0x18, 0x00, 0x00, 0x00,  // mov eax, fs:[0x18]    -> segment FS
        0xA3, 0x10, 0x20, 0x00, 0x00,        // mov [0x2010], eax
    };
    cpu.write(0x1000, code, sizeof code);
    cpu.map(0x00008000, 0x1000);  // bloc désigné par FS
    cpu.write_u32(0x8018, 0xCAFE0018);
    cpu.set_fs_base(0x8000);
    cpu.set_reg(Cpu::Reg::ESP, 0x4800);
    cpu.run(0x1000, 0x1000 + sizeof code);
    const uint32_t a = cpu.read_u32(0x2000), b = cpu.read_u32(0x2004), c = cpu.read_u32(0x2008),
                   esp = cpu.read_u32(0x200C);
    std::printf("entiers : %u (attendu 36)\nflottants x87 : %u (attendu 2)\n"
                "fonction hôte : %u (attendu 5)\npile après l'appel : %#x (attendu 0x4800)\n",
                a, b, c, esp);
    const uint32_t fs = cpu.read_u32(0x2010);
    std::printf("segment FS : %#x (attendu 0xcafe0018)\n", fs);
    const bool ok = a == 36 && b == 2 && c == 5 && esp == 0x4800 && fs == 0xCAFE0018;
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

namespace {

// Temps moyen d'un tour d'une petite boucle émulée, en nanosecondes. Selon les options, chaque
// tour écrit un mot en mémoire (dans une page différente à chaque fois) et appelle une
// fonction hôte.
double loop_time(bool with_store, bool with_call, uint32_t turns) {
    Cpu cpu;
    cpu.map(0x00001000, 0x1000, true);  // code
    cpu.map(0x00002000, 0x3000);        // case d'import et pile
    cpu.map(0x00100000, 0x100000);      // zone d'écriture, 256 pages
    cpu.write_u32(0x3000, cpu.add_host_function({"bench", 0, [](Cpu&) { return 0u; }}));
    std::vector<uint8_t> code = {
        0xB9, 0, 0, 0, 0,              // mov ecx, tours
        0x31, 0xDB,                    // xor ebx, ebx
        // boucle :
        0x89, 0xD8,                    // mov eax, ebx
        0x25, 0x00, 0xF0, 0x0F, 0x00,  // and eax, 0xFF000
    };
    const size_t loop = 7;
    if (with_store) code.insert(code.end(), {0x89, 0x88, 0x00, 0x00, 0x10, 0x00});  // mov [eax+0x100000], ecx
    else code.insert(code.end(), {0x8D, 0x80, 0x00, 0x00, 0x10, 0x00});             // lea eax, [eax+0x100000]
    code.insert(code.end(), {0x81, 0xC3, 0x00, 0x10, 0x00, 0x00});                  // add ebx, 0x1000
    if (with_call) code.insert(code.end(), {0xFF, 0x15, 0x00, 0x30, 0x00, 0x00});   // call [0x3000]
    const int back = static_cast<int>(loop) - static_cast<int>(code.size() + 3);
    code.insert(code.end(), {0x49, 0x75, static_cast<uint8_t>(back)});              // dec ecx ; jnz boucle
    std::memcpy(&code[1], &turns, 4);
    cpu.write(0x1000, code.data(), code.size());
    cpu.set_reg(Cpu::Reg::ESP, 0x4800);
    const auto start = std::chrono::steady_clock::now();
    cpu.run(0x1000, 0x1000 + static_cast<uint32_t>(code.size()));
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() / turns * 1e9;
}

// Temps moyen, en nanosecondes, d'un appel à une fonction du programme qui copie `bytes` octets
// (rep movsd), soit émulée, soit remplacée par une fonction de l'hôte.
double copy_call_time(bool native, uint32_t bytes, uint32_t calls) {
    Cpu cpu;
    cpu.map(0x00001000, 0x1000, true);
    cpu.map(0x00002000, 0x3000);
    cpu.map(0x00100000, 0x100000);
    std::vector<uint8_t> code = {
        0xBB, 0, 0, 0, 0,              // mov ebx, appels
        // boucle :
        0xE8, 0x04, 0x00, 0x00, 0x00,  // call copie
        0x4B,                          // dec ebx
        0x75, 0xF8,                    // jnz boucle
        0xF4,                          // (fin)
        // copie :
        0xBE, 0x00, 0x00, 0x10, 0x00,  // mov esi, 0x100000
        0xBF, 0x00, 0x00, 0x18, 0x00,  // mov edi, 0x180000
        0xB9, 0, 0, 0, 0,              // mov ecx, octets / 4
        0xF3, 0xA5,                    // rep movsd
        0xC3,                          // ret
    };
    const uint32_t words = bytes / 4;
    std::memcpy(&code[1], &calls, 4);
    std::memcpy(&code[25], &words, 4);
    cpu.write(0x1000, code.data(), code.size());
    if (native)
        cpu.replace_function(0x1000 + 14, 0, [bytes](Cpu& c) {
            std::memcpy(c.host(0x180000), c.host(0x100000), bytes);
            return 0u;
        });
    cpu.set_reg(Cpu::Reg::ESP, 0x4800);
    const auto start = std::chrono::steady_clock::now();
    cpu.run(0x1000, 0x1000 + 13);
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() / calls * 1e9;
}

}  // namespace

// Décode tous les fichiers .wav et .mp3 d'un dossier par les chemins du mixeur : le lecteur WAV
// de l'hôte d'abord, AudioToolbox sinon. Un son refusé serait muet en jeu sans autre signe.
int check_audio(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    using zeus::win32::Sound;
    int wav_direct = 0, wav_fallback = 0, mp3 = 0, failed = 0, empty = 0;
    double seconds = 0;
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(root))
        if (entry.is_regular_file()) files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    for (const fs::path& path : files) {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".wav" && ext != ".mp3") continue;
        if (fs::file_size(path) == 0) {  // muet dans le jeu d'origine aussi
            std::printf("vide   %s\n", path.lexically_relative(root).c_str());
            ++empty;
            continue;
        }
        std::shared_ptr<Sound> sound;
        bool direct = false;
        if (ext == ".wav") {
            std::ifstream in(path, std::ios::binary);
            const std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            sound = zeus::win32::decode_wav(reinterpret_cast<const uint8_t*>(data.data()), data.size());
            direct = sound != nullptr;
        }
        if (!sound) {
            sound = zeus::win32::decode_file_async(path);
            while (!sound->ready) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!sound->frames() || sound->rate <= 0) {
            std::printf("ÉCHEC  %s\n", path.lexically_relative(root).c_str());
            ++failed;
            continue;
        }
        seconds += static_cast<double>(sound->frames()) / sound->rate;
        if (ext == ".mp3") ++mp3;
        else if (direct) ++wav_direct;
        else {
            ++wav_fallback;
            std::printf("par AudioToolbox  %s\n", path.lexically_relative(root).c_str());
        }
    }
    std::printf("%d WAV lus par l'hôte, %d WAV par AudioToolbox, %d MP3, %d vides, %d refusés ; %.0f minutes de son\n",
                wav_direct, wav_fallback, mp3, empty, failed, seconds / 60);
    return failed ? 1 : 0;
}

int bench() {
    const double plain = loop_time(false, false, 20000000), store = loop_time(true, false, 20000000);
    const double call = loop_time(false, true, 1000000);
    std::printf("boucle seule         : %6.1f ns par tour\n", plain);
    std::printf("avec une écriture    : %6.1f ns par tour\n", store);
    std::printf("avec un appel hôte   : %6.0f ns par tour\n", call);
    for (uint32_t bytes : {116u, 3480u, 27840u})
        std::printf("copie de %5u octets : %7.0f ns émulée, %6.0f ns par une fonction native\n", bytes,
                    copy_call_time(false, bytes, 20000), copy_call_time(true, bytes, 20000));
    return 0;
}

}  // namespace zeus::run
