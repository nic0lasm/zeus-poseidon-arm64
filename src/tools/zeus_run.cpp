// Hôte du code d'origine du jeu : charge Zeus.exe dans un processeur émulé cloisonné.
//
//   zeus-run --selftest        vérifie le processeur émulé et l'appel de fonctions hôtes
//   zeus-run --bench           mesure le processeur émulé et le coût d'un appel de fonction hôte
//   zeus-run --inspect EXE     charge l'exécutable sans rien exécuter et liste ses imports
//   zeus-run --app [--installer SETUP.exe]
//                              mode application (aussi sans argument) : demande le jeu au
//                              premier lancement (installateur GOG ou dossier déballé),
//                              propose les packs de langue du dossier « Packs », range sauvegardes et réglages dans
//                              ~/Library/Application Support/Zeus Poseidon
//   zeus-run --run EXE [options]
//                              exécute le programme dans le processeur émulé
//     --write-dir DOSSIER      reçoit les écritures du jeu (défaut : run/zeus) ; l'installation
//                              n'est jamais modifiée
//     --language DOSSIER       pack de langue : ses fichiers remplacent ceux de l'installation
//     --trace                  affiche chaque appel système
//     --trace-only PRÉFIXE     ne trace que les fonctions dont le nom commence ainsi (mss32.dll)
//     --break-on FONCTION      arrête le programme à l'appel de cette fonction (essai du rapport)
//     --native                 remplace les fonctions du jeu réécrites pour l'hôte (à l'essai)
//     --deterministic          horloge fictive et son coupé : exécution reproductible
//     --frame-log SORTIE.txt   empreinte de chaque image affichée
//     --profile SORTIE.tsv     profil d'exécution du code du jeu, par bloc (ralentit beaucoup)
//     --counts                 affiche à la fin le nombre d'appels de chaque fonction
//     --headless               fenêtre cachée, son coupé
//     --mute                   son coupé
//     --fullscreen             plein écran dès l'ouverture (⌘F ou ⌘Entrée pour basculer)
//     --frames N, --seconds S  arrête après N images affichées ou S secondes
//     --screenshot SORTIE.bmp  enregistre la dernière image affichée
//     --window-shot SORTIE.bmp enregistre le contenu de la fenêtre, mise à l'échelle comprise
//     --script "T:ACTION; …"   joue des actions à T secondes : click X,Y ; rclick X,Y ;
//                              move X,Y ; key CODE ; char CODE ; shot SORTIE.bmp

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <mach-o/dyld.h>
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

int bench() {
    const double plain = loop_time(false, false, 20000000), store = loop_time(true, false, 20000000);
    const double call = loop_time(false, true, 1000000);
    std::printf("boucle seule         : %6.1f ns par tour\n", plain);
    std::printf("avec une écriture    : %6.1f ns par tour\n", store);
    std::printf("avec un appel hôte   : %6.0f ns par tour\n", call);
    return 0;
}

struct RunOptions {
    std::string exe, write_dir = "run/zeus", profile, frame_log, language, screenshot, window_shot, script, trace_only, break_on;
    bool trace = false, headless = false, counts = false, mute = false, fullscreen = false, deterministic = false,
         native = false;
    int frames = 0;
    double seconds = 0;
};

int run(const RunOptions& o) {
    namespace fs = std::filesystem;
    zeus::win32::Process process(fs::path(o.exe).parent_path(), o.write_dir);
    if (!o.language.empty()) process.vfs().add_language_pack(o.language);
    process.trace = o.trace || !o.trace_only.empty();
    process.trace_only = o.trace_only;
    process.break_on = o.break_on;
    zeus::win32::Desktop& desktop = process.desktop();
    desktop.headless = o.headless;
    desktop.fullscreen = o.fullscreen;
    desktop.virtual_time = o.deterministic;
    desktop.frame_log_path = o.frame_log;
    process.fixed_clock = o.deterministic;
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
    if (!o.profile.empty()) process.cpu().start_profile();
    if (o.native) zeus::win32::install_native_functions(process);
    if (!o.language.empty()) zeus::win32::apply_language_pack(process, o.language);
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
    if (!o.profile.empty()) {
        // Une ligne par bloc de code : adresse, nanosecondes.
        std::ofstream out(o.profile);
        for (const auto& [address, bytes] : process.cpu().profile()) out << std::hex << address << std::dec << '\t' << bytes << '\n';
    }
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

// Vrai si la langue du système est le français : les dialogues du mode application sont
// en français ou en anglais.
bool french() {
    static const bool value = command_output("/usr/bin/defaults read -g AppleLanguages 2>/dev/null").find("\"fr") != std::string::npos;
    return value;
}
const char* tr(const char* fr, const char* en) { return french() ? fr : en; }

// Entoure un texte d'apostrophes pour le passer à l'interpréteur de commandes.
std::string quoted(const std::string& text) {
    std::string out = "'";
    for (char c : text) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}

// Exécute un script AppleScript d'une ligne et renvoie ce qu'il affiche.
std::string applescript(const std::string& script) {
    return command_output("/usr/bin/osascript -e " + quoted(script) + " 2>/dev/null");
}

void alert(const std::string& title, const std::string& text) {
    applescript("display alert \"" + title + "\" message \"" + text + "\"");
}

// Outil de déballage des installateurs GOG : celui du paquet, sinon celui du système.
std::string innoextract_path() {
    namespace fs = std::filesystem;
    char self[4096];
    uint32_t size = sizeof self;
    if (_NSGetExecutablePath(self, &size) == 0) {
        std::error_code error;
        const fs::path bundled = fs::canonical(self, error).parent_path().parent_path() / "Helpers" / "innoextract";
        if (!error && fs::exists(bundled)) return bundled.string();
    }
    for (const char* candidate : {"/opt/homebrew/bin/innoextract", "/usr/local/bin/innoextract"})
        if (fs::exists(candidate)) return candidate;
    return {};
}

// Déballe un installateur GOG dans `destination` et renvoie le chemin de Zeus.exe, ou vide.
std::string unpack_installer(const std::string& installer, const std::filesystem::path& destination) {
    namespace fs = std::filesystem;
    const std::string tool = innoextract_path();
    if (tool.empty()) {
        alert(tr("Outil de déballage introuvable", "Unpacking tool not found"),
              tr("innoextract n'est pas installé.", "innoextract is not installed."));
        return {};
    }
    if (!find_game(destination).empty()) return find_game(destination);  // déjà déballé
    fs::create_directories(destination);
    const std::string command = quoted(tool) + " --silent --extract --output-dir " + quoted(destination.string()) +
                                " " + quoted(installer) + " 2>&1";
    const std::string output = command_output(command);
    const std::string exe = find_game(destination);
    if (exe.empty())
        alert(tr("Cet installateur n'a pas pu être déballé", "This installer could not be unpacked"),
              tr("Il faut l'installateur GOG de Zeus + Poseidon (setup_zeus_and_poseidon_….exe).",
                 "The GOG installer of Zeus + Poseidon is needed (setup_zeus_and_poseidon_….exe)."));
    if (exe.empty() && !output.empty()) std::fprintf(stderr, "%s\n", output.c_str());
    return exe;
}

// Mode application : le jeu est demandé une fois (installateur GOG ou dossier déjà déballé),
// puis retenu. `installer` non vide : on le déballe sans rien demander.
int run_app(const std::string& installer) {
    namespace fs = std::filesystem;
    const char* forced = std::getenv("ZEUS_SUPPORT_DIR");
    const char* home = std::getenv("HOME");
    if (!forced && !home) throw std::runtime_error("dossier personnel introuvable");
    const fs::path support = forced ? fs::path(forced) : fs::path(home) / "Library/Application Support/Zeus Poseidon";
    fs::create_directories(support);
    const fs::path memo = support / "dossier-du-jeu.txt", unpacked = support / "Jeu-origine";
    std::string folder;
    if (std::ifstream in{memo}) std::getline(in, folder);
    std::string exe = folder.empty() ? std::string() : find_game(folder);
    if (!installer.empty()) {
        exe = unpack_installer(installer, unpacked);
        if (exe.empty()) return 1;
        folder = unpacked.string();
    }
    while (exe.empty()) {
        const std::string choice = applescript(
            std::string("button returned of (display dialog \"") +
            tr("Zeus + Poseidon a besoin du jeu d'origine, que vous devez posséder.\\n\\nIndiquez l'installateur GOG "
               "(setup_zeus_and_poseidon_….exe) : il sera déballé, ce qui prend environ une minute et 800 Mo. Ou bien "
               "indiquez un dossier où le jeu est déjà déballé.",
               "Zeus + Poseidon needs the original game, which you must own.\\n\\nPick the GOG installer "
               "(setup_zeus_and_poseidon_….exe): it will be unpacked, which takes about a minute and 800 MB. Or pick "
               "a folder where the game is already unpacked.") +
            "\" buttons {\"" + tr("Quitter", "Quit") + "\", \"" + tr("Dossier…", "Folder…") + "\", \"" +
            tr("Installateur GOG…", "GOG installer…") + "\"} default button 3 with title \"Zeus + Poseidon\")");
        if (choice == tr("Installateur GOG…", "GOG installer…")) {
            const std::string file = applescript(std::string("POSIX path of (choose file with prompt \"") +
                                                 tr("Installateur GOG de Zeus + Poseidon", "GOG installer of Zeus + Poseidon") +
                                                 "\" of type {\"exe\"})");
            if (file.empty()) continue;
            exe = unpack_installer(file, unpacked);
            folder = unpacked.string();
        } else if (choice == tr("Dossier…", "Folder…")) {
            folder = applescript(std::string("POSIX path of (choose folder with prompt \"") +
                                 tr("Dossier du jeu (celui qui contient Zeus.exe)", "Game folder (the one that contains Zeus.exe)") +
                                 "\")");
            if (folder.empty()) continue;
            exe = find_game(folder);
            if (exe.empty())
                alert(tr("Zeus.exe est introuvable dans ce dossier", "Zeus.exe was not found in this folder"),
                      tr("Choisissez le dossier où le jeu a été déballé.", "Pick the folder the game was unpacked into."));
        } else {
            return 0;  // Quitter, ou dialogue fermé
        }
    }
    std::ofstream(memo) << folder << "\n";
    RunOptions o;
    o.exe = exe;
    o.write_dir = (support / "Jeu").string();

    // Packs de langue rangés dans « Packs » : la langue est demandée une fois, puis retenue
    // dans langue.txt (ligne vide : l'anglais de l'édition GOG). Chaque langue a ses sauvegardes.
    std::vector<std::string> packs;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(support / "Packs", error))
        if (fs::exists(entry.path() / "remplace.txt")) packs.push_back(entry.path().filename().string());
    std::sort(packs.begin(), packs.end());
    if (!packs.empty()) {
        const fs::path choice_file = support / "langue.txt";
        std::string choice;
        const bool chosen = static_cast<bool>(std::ifstream(choice_file));
        if (std::ifstream in{choice_file}) std::getline(in, choice);
        if (!chosen) {
            std::string list = "\"English\"";
            for (const std::string& pack : packs) list += ", \"" + pack + "\"";
            const std::string answer = applescript("choose from list {" + list + "} with title \"Zeus + Poseidon\" with prompt \"" +
                                                   tr("Langue du jeu", "Game language") + "\" default items {\"English\"}");
            if (answer == "false") return 0;  // annulé
            choice = answer == "English" ? "" : answer;
            std::ofstream(choice_file) << choice << "\n";
        }
        if (!choice.empty() && std::find(packs.begin(), packs.end(), choice) != packs.end()) {
            o.language = (support / "Packs" / choice).string();
            o.write_dir = (support / ("Jeu-" + choice)).string();
        }
    }
    return run(o);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1 || (argc == 2 && !std::strcmp(argv[1], "--app"))) return run_app({});
        if (argc == 4 && !std::strcmp(argv[1], "--app") && !std::strcmp(argv[2], "--installer")) return run_app(argv[3]);
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
                else if (a == "--language") o.language = value();
                else if (a == "--profile") o.profile = value();
                else if (a == "--frame-log") o.frame_log = value();
                else if (a == "--deterministic") o.deterministic = true;
                else if (a == "--native") o.native = true;
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
