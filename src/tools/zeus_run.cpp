// Hôte du code d'origine du jeu : charge Zeus.exe dans un processeur émulé cloisonné.
//
//   zeus-run --selftest        vérifie le processeur émulé et l'appel de fonctions hôtes
//   zeus-run --bench           mesure le processeur émulé et le coût d'un appel de fonction hôte
//   zeus-run --inspect EXE     charge l'exécutable sans rien exécuter et liste ses imports
//   zeus-run --app             mode application (aussi sans argument) : demande le dossier du jeu
//                              au premier lancement, range sauvegardes et réglages dans
//                              ~/Library/Application Support/Zeus Poseidon
//   zeus-run --run EXE [options]
//                              exécute le programme dans le processeur émulé
//     --write-dir DOSSIER      reçoit les écritures du jeu (défaut : run/zeus) ; l'installation
//                              n'est jamais modifiée
//     --trace                  affiche chaque appel système
//     --trace-only PRÉFIXE     ne trace que les fonctions dont le nom commence ainsi (mss32.dll)
//     --break-on FONCTION      arrête le programme à l'appel de cette fonction (essai du rapport)
//     --counts                 affiche à la fin le nombre d'appels de chaque fonction
//     --headless               fenêtre cachée, son coupé
//     --mute                   son coupé
//     --fullscreen             plein écran dès l'ouverture (⌘F ou ⌘Entrée pour basculer)
//     --frames N, --seconds S  arrête après N images affichées ou S secondes
//     --screenshot SORTIE.bmp  enregistre la dernière image affichée
//     --window-shot SORTIE.bmp enregistre le contenu de la fenêtre, mise à l'échelle comprise
//     --script "T:ACTION; …"   joue des actions à T secondes : click X,Y ; rclick X,Y ;
//                              move X,Y ; key CODE ; char CODE ; shot SORTIE.bmp

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>

#include "win32/cpu.h"
#include "win32/pe_image.h"
#include "win32/process.h"

using zeus::win32::Cpu;

namespace {

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

// Mesure du processeur émulé : une boucle de calcul avec écritures mémoire réparties sur 256
// pages, avec ou sans appel d'une fonction hôte à chaque tour.
int bench() {
    using Clock = std::chrono::steady_clock;
    // Variante : même boucle, écritures toujours dans la même page, puis sans écriture.
    for (int variant = 0; variant < 2; ++variant) {
        Cpu cpu;
        cpu.map(0x00001000, 0x1000, true);
        cpu.map(0x00100000, 0x100000);
        const uint32_t n = 20000000;
        std::vector<uint8_t> code = {0xB9, 0, 0, 0, 0, 0x31, 0xDB,
                                     0x89, 0xD8, 0x25, 0x00, 0x00, 0x00, 0x00};
        if (variant == 0) code.insert(code.end(), {0x89, 0x88, 0x00, 0x00, 0x10, 0x00});
        else code.insert(code.end(), {0x8D, 0x80, 0x00, 0x00, 0x10, 0x00});  // lea, sans écriture
        code.insert(code.end(), {0x81, 0xC3, 0x00, 0x10, 0x00, 0x00});
        code.insert(code.end(), {0x49, 0x75, static_cast<uint8_t>(-static_cast<int>(code.size() - 7 + 3))});
        std::memcpy(&code[1], &n, 4);
        cpu.write(0x1000, code.data(), code.size());
        const auto start = Clock::now();
        cpu.run(0x1000, 0x1000 + static_cast<uint32_t>(code.size()));
        std::printf("%s : %.1f ns par tour\n", variant == 0 ? "écriture, même page" : "sans écriture      ",
                    std::chrono::duration<double>(Clock::now() - start).count() / n * 1e9);
    }
    const uint32_t turns = 2000000;
    double seconds[2] = {0, 0};
    for (int with_call = 0; with_call < 2; ++with_call) {
        Cpu cpu;
        cpu.map(0x00001000, 0x1000, true);
        cpu.map(0x00002000, 0x3000);
        cpu.map(0x00100000, 0x100000);  // zone d'écriture
        const uint32_t nothing = cpu.add_host_function({"bench", 0, [](Cpu&) { return 0u; }});
        cpu.write_u32(0x3000, nothing);
        std::vector<uint8_t> code = {
            0xB9, 0, 0, 0, 0,                    // mov ecx, tours
            0x31, 0xDB,                          // xor ebx, ebx
            // boucle :
            0x89, 0xD8,                          // mov eax, ebx
            0x25, 0x00, 0xF0, 0x0F, 0x00,        // and eax, 0xFF000
            0x89, 0x88, 0x00, 0x00, 0x10, 0x00,  // mov [eax+0x100000], ecx
            0x81, 0xC3, 0x00, 0x10, 0x00, 0x00,  // add ebx, 0x1000
        };
        if (with_call) code.insert(code.end(), {0xFF, 0x15, 0x00, 0x30, 0x00, 0x00});  // call [0x3000]
        const int back = -static_cast<int>(code.size() - 7 + 3);
        code.insert(code.end(), {0x49, 0x75, static_cast<uint8_t>(back)});  // dec ecx ; jnz boucle
        std::memcpy(&code[1], &turns, 4);
        cpu.write(0x1000, code.data(), code.size());
        cpu.set_reg(Cpu::Reg::ESP, 0x4800);
        const auto start = Clock::now();
        cpu.run(0x1000, 0x1000 + static_cast<uint32_t>(code.size()));
        seconds[with_call] = std::chrono::duration<double>(Clock::now() - start).count();
    }
    std::printf("boucle seule      : %.0f ns par tour\n", seconds[0] / turns * 1e9);
    std::printf("avec appel hôte   : %.0f ns par tour\n", seconds[1] / turns * 1e9);
    std::printf("coût d'un appel   : %.0f ns\n", (seconds[1] - seconds[0]) / turns * 1e9);
    return 0;
}

struct RunOptions {
    std::string exe, write_dir = "run/zeus", screenshot, window_shot, script, trace_only, break_on;
    bool trace = false, headless = false, counts = false, mute = false, fullscreen = false;
    int frames = 0;
    double seconds = 0;
};

int run(const RunOptions& o) {
    namespace fs = std::filesystem;
    zeus::win32::Process process(fs::path(o.exe).parent_path(), o.write_dir);
    process.trace = o.trace || !o.trace_only.empty();
    process.trace_only = o.trace_only;
    process.break_on = o.break_on;
    zeus::win32::Desktop& desktop = process.desktop();
    desktop.headless = o.headless;
    desktop.fullscreen = o.fullscreen;
    desktop.screenshot_path = o.screenshot;
    desktop.window_shot_path = o.window_shot;
    desktop.max_presents = o.frames;
    desktop.max_seconds = o.seconds;
    desktop.add_script(o.script);
    zeus::win32::install_kernel32(process);
    zeus::win32::install_user32(process);
    zeus::win32::install_ddraw(process);
    zeus::win32::install_media(process);
    zeus::win32::install_mss(process);
    zeus::win32::install_bink(process);
    process.mute = o.mute || o.headless;
    process.load(o.exe);
    int code = 0;
    std::string error;
    try {
        code = process.run();
    } catch (const std::exception& e) {
        error = e.what();
    }
    desktop.save_screenshot();
    uint64_t total = 0;
    for (const auto& [name, count] : process.call_counts) total += count;
    if (o.counts)
        for (const auto& [name, count] : process.call_counts)
            std::fprintf(stderr, "%10llu  %s\n", static_cast<unsigned long long>(count), name.c_str());
    std::fprintf(stderr, "%llu appels système vers %zu fonctions distinctes, %d images affichées\n",
                 static_cast<unsigned long long>(total), process.call_counts.size(), desktop.presents());
    if (!error.empty()) {
        std::fprintf(stderr, "arrêt : %s\n", error.c_str());
        // Rapport à joindre à un signalement : il ne contient rien de personnel.
        std::error_code ignored;
        fs::create_directories(o.write_dir, ignored);
        const fs::path report = fs::path(o.write_dir) / "dernier-arret.txt";
        if (std::ofstream out{report}) {
            char date[32] = "";
            const std::time_t now = std::time(nullptr);
            std::strftime(date, sizeof date, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
            out << "Zeus + Poseidon sur Mac : arrêt du jeu\n"
                << "date : " << date << "\n"
                << "cause : " << error << "\n"
                << "images affichées : " << desktop.presents() << "\n"
                << "appels système : " << total << " vers " << process.call_counts.size() << " fonctions\n";
        }
        desktop.alert("Zeus + Poseidon",
                      "Le jeu s'est arrêté : il a besoin d'une fonction que ce programme ne fournit pas encore.\n\n" +
                          error + "\n\nRapport enregistré dans :\n" + report.string() +
                          "\n\nMerci de le signaler sur la page du projet.");
        return 1;
    }
    std::fprintf(stderr, "le programme s'est terminé avec le code %d\n", code);
    return code;
}

// Lance une commande et renvoie sa sortie, sans le saut de ligne final.
std::string command_output(const std::string& command) {
    std::string out;
    if (FILE* pipe = popen(command.c_str(), "r")) {
        char buffer[512];
        while (std::fgets(buffer, sizeof buffer, pipe)) out += buffer;
        pclose(pipe);
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

// Cherche Zeus.exe dans un dossier du jeu, ou dans son sous-dossier « app » (installateur GOG déballé).
std::string find_game(const std::filesystem::path& folder) {
    namespace fs = std::filesystem;
    for (const fs::path& dir : {folder, folder / "app"}) {
        std::error_code error;
        for (const auto& entry : fs::directory_iterator(dir, error)) {
            std::string name = entry.path().filename().string();
            for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (name == "zeus.exe") return entry.path().string();
        }
    }
    return {};
}

// Mode application : le dossier du jeu est demandé une fois, puis retenu.
int run_app() {
    namespace fs = std::filesystem;
    const char* forced = std::getenv("ZEUS_SUPPORT_DIR");
    const char* home = std::getenv("HOME");
    if (!forced && !home) throw std::runtime_error("dossier personnel introuvable");
    const fs::path support = forced ? fs::path(forced) : fs::path(home) / "Library/Application Support/Zeus Poseidon";
    fs::create_directories(support);
    const fs::path memo = support / "dossier-du-jeu.txt";
    std::string folder;
    if (std::ifstream in{memo}) std::getline(in, folder);
    std::string exe = folder.empty() ? std::string() : find_game(folder);
    while (exe.empty()) {
        folder = command_output(
            "/usr/bin/osascript -e 'POSIX path of (choose folder with prompt \"Zeus + Poseidon : choisissez le dossier du "
            "jeu (celui qui contient Zeus.exe)\")' 2>/dev/null");
        if (folder.empty()) return 0;  // annulé
        exe = find_game(folder);
        if (exe.empty())
            command_output("/usr/bin/osascript -e 'display alert \"Zeus.exe est introuvable dans ce dossier.\" message "
                           "\"Choisissez le dossier où le jeu a été déballé.\"' >/dev/null 2>&1");
    }
    std::ofstream(memo) << folder << "\n";
    RunOptions o;
    o.exe = exe;
    o.write_dir = (support / "Jeu").string();
    return run(o);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1 || (argc == 2 && !std::strcmp(argv[1], "--app"))) return run_app();
        if (argc >= 3 && !std::strcmp(argv[1], "--run")) {
            RunOptions o;
            o.exe = argv[2];
            for (int i = 3; i < argc; ++i) {
                const std::string a = argv[i];
                auto value = [&]() -> std::string {
                    if (i + 1 >= argc) throw std::runtime_error("valeur manquante après " + a);
                    return argv[++i];
                };
                if (a == "--trace") o.trace = true;
                else if (a == "--trace-only") o.trace_only = value();
                else if (a == "--break-on") o.break_on = value();
                else if (a == "--headless") o.headless = true;
                else if (a == "--counts") o.counts = true;
                else if (a == "--mute") o.mute = true;
                else if (a == "--fullscreen") o.fullscreen = true;
                else if (a == "--write-dir") o.write_dir = value();
                else if (a == "--screenshot") o.screenshot = value();
                else if (a == "--window-shot") o.window_shot = value();
                else if (a == "--script") o.script = value();
                else if (a == "--frames") o.frames = std::stoi(value());
                else if (a == "--seconds") o.seconds = std::stod(value());
                else throw std::runtime_error("option inconnue : " + a);
            }
            return run(o);
        }
        if (argc == 2 && !std::strcmp(argv[1], "--selftest")) return selftest();
        if (argc == 2 && !std::strcmp(argv[1], "--bench")) return bench();
        if (argc == 3 && !std::strcmp(argv[1], "--inspect")) return inspect(argv[2]);
        std::fprintf(stderr, "usage : zeus-run [--app] | --selftest | --inspect EXE | --run EXE [options]\n");
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "zeus-run : %s\n", e.what());
        return 1;
    }
}
