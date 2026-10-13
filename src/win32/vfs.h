// Système de fichiers vu par le programme émulé.
//
// Le programme croit être installé dans C:\Zeus. Les lectures vont dans le dossier d'écriture
// puis dans l'installation du jeu ; toutes les écritures vont dans le dossier d'écriture, de
// sorte que l'installation n'est jamais modifiée. Les noms sont comparés sans tenir compte de
// la casse, comme sous Windows.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace zeus::win32 {

struct VfsEntry {
    std::string name;
    bool is_directory = false;
    uint64_t size = 0;
    uint64_t filetime = 0;  // unités de 100 ns depuis le 1er janvier 1601
};

class Vfs {
public:
    Vfs(std::filesystem::path game_dir, std::filesystem::path write_dir, std::string guest_dir);

    // Fichier ou dossier existant désigné par un chemin Windows, sinon nullopt.
    std::optional<std::filesystem::path> find(const std::string& windows_path) const;
    // Emplacement où écrire ce chemin (dossiers créés), ou nullopt s'il sort de l'installation.
    // Si `keep_content`, le fichier existant de l'installation y est d'abord copié.
    std::optional<std::filesystem::path> writable(const std::string& windows_path, bool keep_content);
    bool remove(const std::string& windows_path);
    bool make_directory(const std::string& windows_path);
    // Entrées correspondant à un motif (jokers * et ? dans le dernier élément du chemin).
    std::vector<VfsEntry> list(const std::string& windows_pattern) const;

    const std::string& current_directory() const { return cwd_; }
    bool set_current_directory(const std::string& windows_path);
    // Chemin Windows absolu et normalisé.
    std::string absolute(const std::string& windows_path) const;

private:
    // Éléments du chemin relatifs à l'installation, en minuscules ; nullopt si en dehors.
    std::optional<std::vector<std::string>> relative_parts(const std::string& windows_path) const;
    static std::optional<std::filesystem::path> lookup(const std::filesystem::path& root,
                                                       const std::vector<std::string>& parts);

    std::filesystem::path game_dir_, write_dir_;
    std::string guest_dir_, cwd_;
    std::set<std::string> removed_;  // fichiers de l'installation supprimés par le programme
};

uint64_t to_filetime(std::filesystem::file_time_type time);

}  // namespace zeus::win32
