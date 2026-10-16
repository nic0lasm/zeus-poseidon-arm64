// Hôte du code d'origine du jeu : charge Zeus.exe dans un processeur émulé cloisonné.
//
//   zeus-run --selftest        vérifie le processeur émulé et l'appel de fonctions hôtes
//   zeus-run --bench           mesure le processeur émulé et le coût d'un appel de fonction hôte
//   zeus-run --inspect EXE     charge l'exécutable sans rien exécuter et liste ses imports
//   zeus-run --check-audio DOSSIER
//                              décode chaque fichier son du dossier comme le fait le mixeur
//   zeus-run --app [--installer SETUP.exe]
//                              mode application (aussi sans argument) : demande le jeu au
//                              premier lancement (installateur GOG ou dossier déballé),
//                              propose les packs de langue du dossier « Packs », range sauvegardes et réglages dans
//                              ~/Library/Application Support/Zeus Poseidon
//   zeus-run --make-pack DOSSIER_CD JEU SORTIE
//                              fabrique un pack de langue depuis les images des CD d'origine
//   zeus-run --run EXE [options]
//                              exécute le programme dans le processeur émulé
//     --write-dir DOSSIER      reçoit les écritures du jeu (défaut : run/zeus) ; l'installation
//                              n'est jamais modifiée
//     --language DOSSIER       pack de langue : ses fichiers remplacent ceux de l'installation
//     --trace                  affiche chaque appel système
//     --trace-only PRÉFIXE     ne trace que les fonctions dont le nom commence ainsi (mss32.dll)
//     --break-on FONCTION      arrête le programme à l'appel de cette fonction (essai du rapport)
//     --no-native              laisse à l'émulateur les fonctions du jeu réécrites pour l'hôte
//     --deterministic          horloge fictive et son coupé : exécution reproductible
//     --wide auto | LxH        remplace le mode 1024 x 768 du jeu par un mode large, à choisir
//                              ensuite dans Options > Display options ; « auto » prend les
//                              proportions de l'écran, et plus de 768 lignes s'il est plus
//                              haut que 16:9
//     --watch-call ADRESSE     liste à la fin les sites d'appel d'une fonction du jeu (hexadécimal)
//     --count ADRESSE[,…]      affiche à la fin le nombre de passages sur chaque adresse du jeu
//                              (hexadécimal), fonctions réécrites en natif comprises
//     --ticks                  affiche à la fin les tours de simulation par seconde, en temps réel
//     --state-log SORTIE.txt   empreinte de l'état de la partie à chaque tour de simulation
//     --verify ADRESSE[:FIN]   exécute chaque appel de cette fonction du jeu (ou ce morceau de
//                              code) deux fois, original puis natif, et compare registres et
//                              mémoire ; s'arrête à la première différence (hexadécimal)
//     --verify-every N         ne vérifie qu'un passage sur N
//     --dump-data SORTIE.bin   écrit à la fin la section .data du programme (variables du jeu)
//     --frame-log SORTIE.txt   empreinte de chaque image affichée
//     --profile SORTIE.tsv     profil d'exécution du code du jeu, par bloc (ralentit beaucoup)
//     --call-graph SORTIE.tsv  arbre d'appels et temps par bloc, pendant le tour de simulation ou non
//     --time-span A:R,…        temps passé de l'entrée A d'une fonction à son retour R (hexadécimal)
//     --counts                 affiche à la fin le nombre d'appels et le temps passé dans chaque fonction
//     --headless               fenêtre cachée, son coupé
//     --mute                   son coupé
//     --fullscreen             plein écran dès l'ouverture (⌘F ou ⌘Entrée pour basculer)
//     --frames N, --seconds S  arrête après N images affichées ou S secondes
//     --screenshot SORTIE.bmp  enregistre la dernière image affichée
//     --window-shot SORTIE.bmp enregistre le contenu de la fenêtre, mise à l'échelle comprise
//     --script "T:ACTION; …"   joue des actions à T secondes : click X,Y ; rclick X,Y ;
//                              move X,Y ; down X,Y ; up X,Y ; wheel N ; blur ; focus ;
//                              key CODE ; char CODE ; shot SORTIE.bmp

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#include "tools/zeus_run/zeus_run.h"

using namespace zeus::run;

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
                else if (a == "--call-graph") o.call_graph = value();
                else if (a == "--time-span") o.time_span = value();
                else if (a == "--frame-log") o.frame_log = value();
                else if (a == "--dump-data") o.memory_dump = value();
                else if (a == "--wide") o.wide = value();
                else if (a == "--watch-call") o.watch = value();
                else if (a == "--count") o.count = value();
                else if (a == "--ticks") o.ticks = true;
                else if (a == "--state-log") o.state_log = value();
                else if (a == "--verify") o.verify = value();
                else if (a == "--verify-every") o.verify_every = static_cast<uint32_t>(std::stoul(value()));
                else if (a == "--deterministic") o.deterministic = true;
                else if (a == "--native") o.native = true;
                else if (a == "--no-native") o.native = false;
                else if (a == "--screenshot") o.screenshot = value();
                else if (a == "--window-shot") o.window_shot = value();
                else if (a == "--script") o.script = value();
                else if (a == "--frames") o.frames = std::stoi(value());
                else if (a == "--seconds") o.seconds = std::stod(value());
                else throw std::runtime_error("option inconnue : " + a);
            }
            return run(o);
        }
        if (argc == 5 && !std::strcmp(argv[1], "--make-pack")) {
            const std::string failure = make_language_pack(argv[2], argv[3], argv[4]);
            if (!failure.empty()) std::fprintf(stderr, "zeus-run : %s\n", failure.c_str());
            return failure.empty() ? 0 : 1;
        }
        if (argc == 2 && !std::strcmp(argv[1], "--selftest")) return selftest();
        if (argc == 2 && !std::strcmp(argv[1], "--bench")) return bench();
        if (argc == 3 && !std::strcmp(argv[1], "--inspect")) return inspect(argv[2]);
        if (argc == 3 && !std::strcmp(argv[1], "--check-audio")) return check_audio(argv[2]);
        std::fprintf(stderr, "usage : zeus-run [--app] | --selftest | --bench | --inspect EXE | --make-pack CD JEU SORTIE | --run EXE [options]\n");
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "zeus-run : %s\n", e.what());
        return 1;
    }
}
