// KERNEL32 : état partagé par kernel32.cpp, kernel32_memory.cpp et kernel32_files.cpp.

#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "win32/process.h"
#include "win32/vfs.h"

namespace zeus::win32::kernel32 {

namespace fs = std::filesystem;

constexpr uint32_t kInvalidHandle = 0xFFFFFFFF;
constexpr uint32_t kErrorFileNotFound = 2, kErrorPathNotFound = 3, kErrorInvalidHandle = 6,
                   kErrorNoMoreFiles = 18, kErrorAlreadyExists = 183;

// Fichier ouvert. Un fichier de l'installation, que le programme ne peut pas modifier, est lu
// une fois pour toutes en mémoire : le jeu rouvre sans cesse les mêmes (plusieurs milliers de
// fois par minute pour un fichier d'aventure).
struct OpenFile {
    std::fstream stream;
    fs::path path;
    std::shared_ptr<const std::vector<uint8_t>> data;  // contenu en mémoire, ou nul
    size_t position = 0;
};

struct FindState {
    std::vector<VfsEntry> entries;
    size_t next = 0;
};

struct State {
    explicit State(Process& p) : vfs(p.vfs()) {}
    Vfs& vfs;
    std::map<uint32_t, std::unique_ptr<OpenFile>> files;
    // Contenu des fichiers de l'installation déjà lus, jusqu'à 4 Mo chacun et 512 Mo en tout : le
    // jeu rouvre sans cesse ses petits fichiers (cartes, réglages, sons d'ambiance), mais ne lit
    // qu'une fois par partie ses gros fichiers d'images, qu'il garde lui-même en mémoire.
    std::map<std::string, std::shared_ptr<const std::vector<uint8_t>>> contents;
    size_t contents_size = 0;
    std::map<uint32_t, FindState> finds;
    std::map<uint32_t, uint32_t> virtual_regions;  // adresse -> taille
    std::map<std::string, std::string> environment;
    uint32_t next_handle = 0x100;
    uint32_t command_line = 0, environment_a = 0, environment_w = 0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

inline std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Fonctions de chaque partie, appelées par install_kernel32 dans cet ordre.
void install_memory(Process& p, const std::shared_ptr<State>& st);  // tas, mémoire, jeux de caractères
void install_files(Process& p, const std::shared_ptr<State>& st);   // fichiers et dossiers

}  // namespace zeus::win32::kernel32
