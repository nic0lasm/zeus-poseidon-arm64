#include "win32/vfs.h"

#include "win32/text.h"

#include <fstream>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <system_error>

namespace zeus::win32 {
namespace {

namespace fs = std::filesystem;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::vector<std::string> split(const std::string& path) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : path) {
        if (c == '\\' || c == '/') {
            if (!current.empty()) parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) parts.push_back(current);
    return parts;
}

std::string join(const std::vector<std::string>& parts) {
    std::string out;
    for (const std::string& p : parts) out += (out.empty() ? "" : "\\") + p;
    return out;
}

// Motif Windows : * = suite quelconque, ? = un caractère ; « *.* » accepte tous les noms.
bool wildcard(const std::string& pattern, const std::string& name) {
    if (pattern == "*.*" || pattern == "*") return true;
    size_t p = 0, n = 0, star = std::string::npos, mark = 0;
    while (n < name.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == name[n])) {
            ++p;
            ++n;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = n;
        } else if (star != std::string::npos) {
            p = star + 1;
            n = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

}  // namespace

uint64_t to_filetime(fs::file_time_type time) {
    using namespace std::chrono;
    const auto system = time_point_cast<system_clock::duration>(time - fs::file_time_type::clock::now() +
                                                                system_clock::now());
    const int64_t unix_100ns = duration_cast<nanoseconds>(system.time_since_epoch()).count() / 100;
    return static_cast<uint64_t>(unix_100ns + 116444736000000000LL);
}

Vfs::Vfs(fs::path game_dir, fs::path write_dir, std::string guest_dir)
    : game_dir_(std::move(game_dir)), write_dir_(std::move(write_dir)),
      guest_dir_(std::move(guest_dir)), cwd_(guest_dir_) {
    fs::create_directories(write_dir_);
}

std::string Vfs::absolute(const std::string& windows_path) const {
    std::string full = windows_path;
    const bool has_drive = full.size() >= 2 && full[1] == ':';
    if (!has_drive) {
        if (!full.empty() && (full[0] == '\\' || full[0] == '/')) full = cwd_.substr(0, 2) + full;
        else full = cwd_ + "\\" + full;
    }
    std::vector<std::string> out;
    for (const std::string& part : split(full.substr(2))) {
        if (part == ".") continue;
        if (part == "..") {
            if (!out.empty()) out.pop_back();
            continue;
        }
        out.push_back(part);
    }
    std::string drive = full.substr(0, 2);
    drive[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(drive[0])));
    return drive + "\\" + join(out);
}

std::optional<std::vector<std::string>> Vfs::relative_parts(const std::string& windows_path) const {
    const std::string full = lower(absolute(windows_path));
    const std::string root = lower(guest_dir_);
    if (full.compare(0, root.size(), root) != 0) return std::nullopt;
    if (full.size() > root.size() && full[root.size()] != '\\') return std::nullopt;
    return split(full.substr(root.size()));
}

std::optional<fs::path> Vfs::lookup(const fs::path& root, const std::vector<std::string>& parts) {
    fs::path current = root;
    std::error_code ec;
    for (const std::string& part : parts) {
        bool found = false;
        for (const auto& entry : fs::directory_iterator(current, ec)) {
            if (lower(from_utf8(entry.path().filename().string())) == part) {
                current = entry.path();
                found = true;
                break;
            }
        }
        if (ec || !found) return std::nullopt;
    }
    return fs::exists(current, ec) ? std::optional<fs::path>(current) : std::nullopt;
}

std::optional<fs::path> Vfs::lookup_fixed(const fs::path& root, const std::vector<std::string>& parts) const {
    const std::string key = root.string() + '\n' + join(parts);
    const auto known = fixed_cache_.find(key);
    if (known != fixed_cache_.end()) return known->second;
    return fixed_cache_[key] = lookup(root, parts);
}

void Vfs::add_language_pack(const fs::path& pack) {
    pack_dir_ = pack;
    replaced_dirs_.clear();
    std::ifstream in(pack / "remplace.txt");
    for (std::string line; std::getline(in, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) replaced_dirs_.insert(lower(line));
    }
}

bool Vfs::replaced(const std::vector<std::string>& parts) const {
    return !parts.empty() && replaced_dirs_.count(parts[0]);
}

std::optional<fs::path> Vfs::lookup_base(const std::vector<std::string>& parts) const {
    if (!pack_dir_.empty()) {
        if (auto found = lookup_fixed(pack_dir_, parts)) return found;
        if (replaced(parts)) return std::nullopt;
    }
    return lookup_fixed(game_dir_, parts);
}

std::optional<fs::path> Vfs::find(const std::string& windows_path) const {
    const auto parts = relative_parts(windows_path);
    if (!parts) return std::nullopt;
    if (auto written = lookup(write_dir_, *parts)) return written;
    if (removed_.count(join(*parts))) return std::nullopt;
    return lookup_base(*parts);
}

std::optional<fs::path> Vfs::writable(const std::string& windows_path, bool keep_content) {
    const auto parts = relative_parts(windows_path);
    if (!parts || parts->empty()) return std::nullopt;
    if (auto written = lookup(write_dir_, *parts)) return written;
    // Reprend la casse des dossiers déjà présents dans l'installation.
    fs::path target = write_dir_;
    std::vector<std::string> walked;
    const std::vector<std::string> real = split(absolute(windows_path).substr(guest_dir_.size()));
    for (size_t i = 0; i < parts->size(); ++i) {
        walked.push_back((*parts)[i]);
        const auto existing = lookup_base(walked);
        target /= existing ? existing->filename().string() : to_utf8(real[i]);
    }
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    if (keep_content && !removed_.count(join(*parts))) {
        if (const auto original = lookup_base(*parts); original && fs::is_regular_file(*original))
            fs::copy_file(*original, target, fs::copy_options::overwrite_existing, ec);
    }
    removed_.erase(join(*parts));
    return target;
}

bool Vfs::remove(const std::string& windows_path) {
    const auto parts = relative_parts(windows_path);
    if (!parts || parts->empty()) return false;
    bool done = false;
    std::error_code ec;
    if (auto written = lookup(write_dir_, *parts)) done = fs::remove(*written, ec);
    if (lookup_base(*parts) && !removed_.count(join(*parts))) {
        removed_.insert(join(*parts));  // l'installation reste intacte : on masque le fichier
        done = true;
    }
    return done;
}

bool Vfs::make_directory(const std::string& windows_path) {
    if (find(windows_path)) return false;
    const auto target = writable(windows_path + "\\x", false);
    if (!target) return false;
    std::error_code ec;
    fs::create_directories(target->parent_path(), ec);
    return !ec;
}

std::vector<VfsEntry> Vfs::list(const std::string& windows_pattern) const {
    std::vector<VfsEntry> result;
    const std::string full = absolute(windows_pattern);
    const size_t slash = full.find_last_of('\\');
    const std::string pattern = lower(full.substr(slash + 1));
    const auto parts = relative_parts(full.substr(0, slash));
    if (!parts) return result;
    std::map<std::string, VfsEntry> merged;
    std::error_code ec;
    // Du moins prioritaire au plus prioritaire : installation, pack de langue, écritures.
    for (const fs::path& root : {game_dir_, pack_dir_, write_dir_}) {
        if (root.empty() || (root == game_dir_ && replaced(*parts))) continue;
        const auto dir = root == write_dir_ ? lookup(root, *parts) : lookup_fixed(root, *parts);
        if (!dir || !fs::is_directory(*dir, ec)) continue;
        for (const auto& entry : fs::directory_iterator(*dir, ec)) {
            const std::string name = from_utf8(entry.path().filename().string());
            if (name == ".DS_Store" || !wildcard(pattern, lower(name))) continue;
            std::vector<std::string> key = *parts;
            key.push_back(lower(name));
            if (root != write_dir_ && removed_.count(join(key))) continue;
            VfsEntry e;
            e.name = name;
            e.is_directory = entry.is_directory(ec);
            e.size = e.is_directory ? 0 : entry.file_size(ec);
            e.filetime = to_filetime(entry.last_write_time(ec));
            merged[lower(name)] = e;
        }
    }
    const bool has_wildcard = pattern.find_first_of("*?") != std::string::npos;
    if (has_wildcard && !merged.empty() && wildcard(pattern, ".")) {
        VfsEntry dot;
        dot.is_directory = true;
        dot.name = ".";
        result.push_back(dot);
        dot.name = "..";
        result.push_back(dot);
    }
    for (auto& [name, entry] : merged) result.push_back(entry);
    return result;
}

bool Vfs::set_current_directory(const std::string& windows_path) {
    const auto found = find(windows_path);
    std::error_code ec;
    if (!found || !fs::is_directory(*found, ec)) return false;
    cwd_ = absolute(windows_path);
    return true;
}

}  // namespace zeus::win32
