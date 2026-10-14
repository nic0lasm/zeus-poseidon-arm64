// Système de fichiers vu par le programme émulé.
//
// Le programme croit être installé dans C:\Zeus. Les lectures vont dans le dossier d'écriture
// puis dans l'installation du jeu ; toutes les écritures vont dans le dossier d'écriture, de
// sorte que l'installation n'est jamais modifiée. Les noms sont comparés sans tenir compte de
// la casse, comme sous Windows.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
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

    // Ajoute un pack de langue : un dossier dont les fichiers remplacent ceux de même nom de
    // l'installation. Son fichier « remplace.txt » liste, un par ligne, les dossiers de
    // premier niveau qu'il remplace en entier (ceux de l'installation deviennent invisibles).
    void add_language_pack(const std::filesystem::path& pack);

    // Fichier ou dossier existant désigné par un chemin Windows, sinon nullopt.
    std::optional<std::filesystem::path> find(const std::string& windows_path) const;
    // Emplacement où écrire ce chemin (dossiers créés), ou nullopt s'il sort de l'installation.
    // Si `keep_content`, le fichier existant de l'installation y est d'abord copié.
    std::optional<std::filesystem::path> writable(const std::string& windows_path, bool keep_content);
    // Vrai si ce fichier de l'hôte appartient à l'installation ou au pack de langue, que le
    // programme ne peut pas modifier.
    bool is_fixed(const std::filesystem::path& host_path) const;
    bool remove(const std::string& windows_path);
    bool make_directory(const std::string& windows_path);
    // Entrées correspondant à un motif (jokers * et ? dans le dernier élément du chemin).
    std::vector<VfsEntry> list(const std::string& windows_pattern) const;

    const std::string& current_directory() const { return cwd_; }
    bool set_current_directory(const std::string& windows_path);
    // Chemin Windows absolu et normalisé.
    std::string absolute(const std::string& windows_path) const;

private:
    // Éléments du chemin relatifs à l'installation, en minuscules et dans le codage du jeu
    // (Windows-1252) ; nullopt si en dehors. Les noms de fichiers de l'hôte sont en UTF-8.
    std::optional<std::vector<std::string>> relative_parts(const std::string& windows_path) const;
    std::optional<std::filesystem::path> lookup(const std::filesystem::path& root,
                                                const std::vector<std::string>& parts) const;
    // Contenu d'un dossier de l'hôte : nom en minuscules dans le codage du jeu -> nom réel. Le
    // résultat est retenu tant que la date de modification du dossier ne change pas.
    struct Listing {
        std::filesystem::file_time_type modified;
        std::map<std::string, std::string> names;
    };
    const std::map<std::string, std::string>& listing(const std::filesystem::path& directory) const;
    mutable std::map<std::string, Listing> listings_;

    // Comme lookup, pour un dossier qui ne change jamais (installation, pack de langue) : le
    // résultat est retenu, car le jeu redemande sans cesse les mêmes fichiers.
    std::optional<std::filesystem::path> lookup_fixed(const std::filesystem::path& root,
                                                      const std::vector<std::string>& parts) const;
    mutable std::map<std::string, std::optional<std::filesystem::path>> fixed_cache_;

    // Recherche dans le pack de langue puis dans l'installation.
    std::optional<std::filesystem::path> lookup_base(const std::vector<std::string>& parts) const;
    bool replaced(const std::vector<std::string>& parts) const;

    std::filesystem::path game_dir_, write_dir_, pack_dir_;
    std::set<std::string> replaced_dirs_;  // dossiers de premier niveau fournis en entier par le pack
    std::string guest_dir_, cwd_;
    std::set<std::string> removed_;  // fichiers de l'installation supprimés par le programme
};

uint64_t to_filetime(std::filesystem::file_time_type time);

}  // namespace zeus::win32
