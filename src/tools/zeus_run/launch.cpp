// Lancement du jeu dans le processeur émulé, avec les options de --run.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tools/zeus_run/zeus_run.h"
#include "win32/cpu.h"
#include "win32/game_gog.h"
#include "win32/process.h"

namespace zeus::run {

using zeus::win32::Cpu;

int run(const RunOptions& o) {
    namespace fs = std::filesystem;
    zeus::win32::Process process(fs::path(o.exe).parent_path(), o.write_dir);
    if (!o.language.empty()) process.vfs().add_language_pack(o.language);
    process.trace = o.trace || !o.trace_only.empty();
    process.trace_only = o.trace_only;
    process.break_on = o.break_on;
    process.time_calls = o.counts;
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
    // Appels d'une fonction du jeu, regroupés par site d'appel : nombre, et arguments du dernier.
    struct Caller {
        uint64_t count = 0;
        uint32_t args[6] = {};
    };
    auto callers = std::make_shared<std::map<uint32_t, Caller>>();
    if (!o.watch.empty()) {
        process.cpu().add_code_hook(static_cast<uint32_t>(std::stoul(o.watch, nullptr, 16)), [callers](zeus::win32::Cpu& c) {
            Caller& caller = (*callers)[c.read_u32(c.reg(zeus::win32::Cpu::Reg::ESP))];
            ++caller.count;
            for (int i = 0; i < 6; ++i) caller.args[i] = c.arg(i);
        });
    }
    if (!o.wide.empty()) {
        // « auto » : les proportions de l'écran, avec la largeur d'un écran 16:9 en 768 lignes
        // (1366) ; un écran plus haut (16:10, 3:2, 4:3) gagne des lignes, jusqu'à 1023.
        int w = 0, h = 768;
        if (o.wide == "auto") {
            const double aspect = desktop.screen_aspect();
            h = std::clamp(static_cast<int>(1366 / aspect), 768, 1023);
            w = static_cast<int>(h * aspect);
        }
        else if (std::sscanf(o.wide.c_str(), "%dx%d", &w, &h) != 2) throw std::runtime_error("--wide attend « auto » ou LARGEURxHAUTEUR");
        if (w >= 1084 && zeus::win32::apply_wide_mode(process, w, h))
            std::fprintf(stderr, "[hôte] écran large : le mode 1024 x 768 du jeu devient %d x %d\n", w, h);
    }
    if (o.native) zeus::win32::install_native_functions(process);
    if (!o.language.empty()) zeus::win32::apply_language_pack(process, o.language);
    // Compteurs de passages, posés en dernier : ils s'ajoutent aux interceptions déjà en place.
    std::vector<std::pair<uint32_t, std::shared_ptr<uint64_t>>> counters;
    for (size_t start = 0; start < o.count.size();) {
        const size_t end = std::min(o.count.find(',', start), o.count.size());
        const uint32_t address = static_cast<uint32_t>(std::stoul(o.count.substr(start, end - start), nullptr, 16));
        auto passes = std::make_shared<uint64_t>(0);
        process.cpu().add_code_hook(address, [passes](Cpu&) { ++*passes; });
        counters.emplace_back(address, passes);
        start = end + 1;
    }
    // Tours de simulation, en temps réel : les intervalles de plus d'une seconde (jeu en pause,
    // message ouvert, chargement) ne comptent pas dans la durée.
    struct Ticks {
        uint64_t count = 0, timed = 0;
        double seconds = 0;
        std::chrono::steady_clock::time_point last;
    };
    auto ticks = std::make_shared<Ticks>();
    if (o.ticks)
        process.cpu().add_code_hook(zeus::win32::gog::kSimulationTick, [ticks](Cpu&) {
            const auto now = std::chrono::steady_clock::now();
            const double gap = std::chrono::duration<double>(now - ticks->last).count();
            if (ticks->count++ > 0 && gap < 1.0) {
                ticks->seconds += gap;
                ++ticks->timed;
            }
            ticks->last = now;
        });
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
    for (const auto& [address, caller] : *callers)
        std::fprintf(stderr, "appelant %#x : %llu appels, derniers arguments %d %d %d %d %d %d\n", address,
                     static_cast<unsigned long long>(caller.count), static_cast<int32_t>(caller.args[0]),
                     static_cast<int32_t>(caller.args[1]), static_cast<int32_t>(caller.args[2]),
                     static_cast<int32_t>(caller.args[3]), static_cast<int32_t>(caller.args[4]),
                     static_cast<int32_t>(caller.args[5]));
    for (const auto& [address, passes] : counters)
        std::fprintf(stderr, "passages %#010x : %llu\n", address, static_cast<unsigned long long>(*passes));
    if (o.ticks)
        std::fprintf(stderr, "tours de simulation : %llu, %.1f par seconde sur %.1f s de jeu actif (temps réel%s)\n",
                     static_cast<unsigned long long>(ticks->count), ticks->seconds > 0 ? ticks->timed / ticks->seconds : 0.0,
                     ticks->seconds, o.deterministic ? ", sans valeur sous --deterministic" : "");
    if (!o.memory_dump.empty()) {
        // Section de données de l'exécutable, telle qu'elle est à la fin de l'exécution.
        std::ofstream out(o.memory_dump, std::ios::binary);
        for (const auto& s : process.image().sections)
            if (s.name == ".data") out.write(reinterpret_cast<const char*>(process.cpu().host(s.address)), s.virtual_size);
    }
    if (!o.profile.empty()) {
        // Une ligne par bloc de code : adresse, nanosecondes, nombre d'exécutions, taille en octets.
        std::ofstream out(o.profile);
        for (const auto& [address, block] : process.cpu().profile())
            out << std::hex << address << std::dec << '\t' << block.nanoseconds << '\t' << block.executions << '\t' << block.bytes << '\n';
    }
    if (o.counts)
        for (const auto& [name, count] : process.call_counts) {
            const auto time = process.call_times.find(name);
            std::fprintf(stderr, "%10llu  %8.1f ms  %s\n", static_cast<unsigned long long>(count),
                         time == process.call_times.end() ? 0.0 : time->second / 1e6, name.c_str());
        }
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

}  // namespace zeus::run
