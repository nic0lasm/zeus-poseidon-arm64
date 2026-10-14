// Processus Windows minimal autour du code d'origine : mémoire, pile, imports, tas, poignées.
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "win32/audio.h"
#include "win32/cpu.h"
#include "win32/desktop.h"
#include "win32/pe_image.h"
#include "win32/vfs.h"

namespace zeus::win32 {

// Chemin sous lequel le programme émulé croit être installé.
inline const char* const kGuestDir = "C:\\Zeus";

class Process {
public:
    // `game_dir` : installation du jeu, jamais modifiée. `write_dir` : reçoit tout ce que le
    // programme écrit (sauvegardes, réglages) et masque les fichiers de même nom de game_dir.
    Process(std::filesystem::path game_dir, std::filesystem::path write_dir);

    void load(const std::filesystem::path& exe);
    // Exécute depuis le point d'entrée jusqu'à la fin du programme ; renvoie son code de sortie.
    int run();

    Cpu& cpu() { return cpu_; }
    const PeImage& image() const { return image_; }
    const std::filesystem::path& game_dir() const { return game_dir_; }
    const std::filesystem::path& write_dir() const { return write_dir_; }

    // Déclare une fonction système. `arg_bytes` : octets d'arguments dépilés (stdcall).
    void def(const std::string& dll, const std::string& name, uint32_t arg_bytes,
             std::function<uint32_t()> fn);
    uint32_t arg(int n) const { return cpu_.arg(n); }
    // Fonction hôte hors table d'import (méthode d'un objet COM, par exemple) : renvoie
    // l'adresse émulée à placer dans une table de pointeurs de fonctions.
    uint32_t host_function(const std::string& name, uint32_t arg_bytes, std::function<uint32_t()> fn);

    // Bibliothèque dont les fonctions non écrites, de nom décoré « _Nom@N », renvoient 0 au lieu
    // d'arrêter le programme (son et vidéo, en attendant leur réalisation).
    void allow_silent_stubs(const std::string& dll) { silent_dlls_.push_back(dll); }

    Vfs& vfs() { return vfs_; }
    // Remplacement d'octets, de même longueur, dans le contenu d'un fichier de l'installation tel
    // que le programme le lit (le fichier lui-même n'est pas touché). `file` : nom sans dossier,
    // en minuscules.
    struct FilePatch {
        std::string file, from, to;
    };
    void add_file_patch(FilePatch patch);
    const std::vector<FilePatch>& file_patches() const { return file_patches_; }
    // Fenêtre et écran, créés à la première utilisation.
    Desktop& desktop();
    // Sortie son, ouverte à la première utilisation ; nul si elle n'a pas pu l'être.
    Mixer* mixer();
    // Millisecondes écoulées, base commune de GetTickCount et timeGetTime.
    uint32_t ticks() const;

    // Tas du programme émulé.
    uint32_t alloc(uint32_t size, bool zero = true);
    void release(uint32_t address);
    uint32_t alloc_size(uint32_t address) const;  // 0 si l'adresse n'est pas un bloc alloué
    uint32_t heap_end() const { return heap_next_; }  // fin de la partie du tas déjà distribuée
    // Copie une chaîne dans le tas et renvoie son adresse.
    uint32_t alloc_string(const std::string& text);

    void exit(int code);

    // Envoie ces messages à la procédure de fenêtre, l'un après l'autre, avant que la fonction
    // système en cours ne rende la main au programme avec la valeur `result`.
    void send_messages(std::vector<Message> messages, uint32_t result);

    uint32_t last_error = 0;
    bool trace = false;
    bool mute = false;  // son mélangé mais inaudible
    // Horloge fictive : chaque lecture de l'heure avance d'une milliseconde, rien n'attend et le
    // son est coupé. Deux exécutions du même scénario donnent alors les mêmes images, ce qui
    // permet de comparer deux versions de l'hôte image par image.
    bool fixed_clock = false;
    void advance_clock(uint32_t milliseconds) const { fixed_ticks_ += milliseconds; }
    std::string break_on;    // diagnostic : arrête le programme à l'appel de cette fonction
    std::string trace_only;  // si non vide : ne trace que les fonctions dont le nom commence ainsi
    bool traced(const std::string& name) const {
        return trace && (trace_only.empty() || name.compare(0, trace_only.size(), trace_only) == 0);
    }
    std::map<std::string, uint64_t> call_counts;
    // Temps passé dans chaque fonction système, en nanosecondes (mesuré si `time_calls`).
    bool time_calls = false;
    std::map<std::string, uint64_t> call_times;

private:
    void bind_imports();

    std::filesystem::path game_dir_, write_dir_;
    mutable Cpu cpu_;
    Vfs vfs_;
    std::vector<std::string> silent_dlls_;
    std::vector<FilePatch> file_patches_;
    PeImage image_;
    std::map<std::string, HostFunction> functions_;  // clé « dll!fonction », dll en minuscules
    std::map<uint32_t, uint32_t> blocks_;            // adresse -> taille
    std::multimap<uint32_t, uint32_t> free_blocks_;  // taille -> adresse
    uint32_t heap_next_ = 0;
    std::unique_ptr<Desktop> desktop_;
    std::unique_ptr<Mixer> mixer_;
    bool mixer_failed_ = false;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
    int exit_code_ = 0;
    mutable uint32_t fixed_ticks_ = 0;
};

// Chaque module déclare les fonctions d'une bibliothèque Windows.
// Adapte l'affichage du texte à un pack de langue ; à appeler après le chargement de l'exécutable.
void apply_language_pack(Process& process, const std::filesystem::path& pack);

// Remplace le mode 1024x768 du jeu par un mode large ; à appeler après le chargement. La taille
// est arrondie à ce que le jeu sait afficher ; renvoie faux si rien n'a été modifié.
bool apply_wide_mode(Process& process, int& width, int& height);

// Remplace les fonctions du jeu réécrites pour l'hôte ; à appeler après le chargement.
void install_native_functions(Process& process);

void install_kernel32(Process& process);
void install_user32(Process& process);   // et GDI32, WINMM, OLE32, SHELL32
void install_ddraw(Process& process);
void install_media(Process& process);    // IJL10 : images JPEG
void install_bink(Process& process);     // BINKW32 : vidéos
void install_mss(Process& process);      // MSS32 : son

}  // namespace zeus::win32
