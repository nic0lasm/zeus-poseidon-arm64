// KERNEL32 : fichiers et dossiers pour le programme émulé, à travers le Vfs.

#include <cstdio>
#include <cstring>

#include "win32/kernel32_state.h"

namespace zeus::win32::kernel32 {
namespace {

void write_find_data(Cpu& cpu, uint32_t address, const VfsEntry& e) {
    cpu.fill(address, 0, 320);
    cpu.write_u32(address, e.is_directory ? 0x10 : 0x20);  // DIRECTORY ou ARCHIVE
    for (uint32_t offset : {4u, 12u, 20u}) {               // création, accès, écriture
        cpu.write_u32(address + offset, static_cast<uint32_t>(e.filetime));
        cpu.write_u32(address + offset + 4, static_cast<uint32_t>(e.filetime >> 32));
    }
    cpu.write_u32(address + 28, static_cast<uint32_t>(e.size >> 32));
    cpu.write_u32(address + 32, static_cast<uint32_t>(e.size));
    cpu.write_string(address + 44, e.name.substr(0, 259));
}

}  // namespace

void install_files(Process& p, const std::shared_ptr<State>& st) {
    Cpu& cpu = p.cpu();
    auto def = [&](const char* name, uint32_t args, std::function<uint32_t()> fn) {
        p.def("kernel32.dll", name, args * 4, std::move(fn));
    };

    // --- fichiers ----------------------------------------------------------------------
    def("CreateFileA", 7, [&p, &cpu, st] {
        const std::string name = cpu.read_string(p.arg(0));
        const uint32_t access = p.arg(1), disposition = p.arg(4);
        if (p.trace) std::fprintf(stderr, "    fichier « %s » accès %#x mode %u\n", name.c_str(), access, disposition);
        const bool write = (access & 0x40000000) != 0;
        const auto existing = st->vfs.find(name);
        // 1 CREATE_NEW, 2 CREATE_ALWAYS, 3 OPEN_EXISTING, 4 OPEN_ALWAYS, 5 TRUNCATE_EXISTING
        if ((disposition == 3 || disposition == 5) && !existing) {
            p.last_error = kErrorFileNotFound;
            return kInvalidHandle;
        }
        if (disposition == 1 && existing) {
            p.last_error = kErrorAlreadyExists;
            return kInvalidHandle;
        }
        auto file = std::make_unique<OpenFile>();
        if (write || disposition == 2) {
            const bool truncate = disposition == 2 || disposition == 5 || disposition == 1;
            const auto target = st->vfs.writable(name, !truncate);
            if (!target) {
                p.last_error = kErrorPathNotFound;
                return kInvalidHandle;
            }
            file->path = *target;
            if (truncate || !fs::exists(*target)) std::ofstream(*target, std::ios::binary).close();
            file->stream.open(*target, std::ios::binary | std::ios::in | std::ios::out);
        } else {
            if (fs::is_directory(*existing)) {
                p.last_error = 5;  // ERROR_ACCESS_DENIED
                return kInvalidHandle;
            }
            file->path = *existing;
            std::error_code ec;
            const uintmax_t size = st->vfs.is_fixed(*existing) ? fs::file_size(*existing, ec) : 0;
            const auto known = st->contents.find(existing->string());
            if (known != st->contents.end()) {
                file->data = known->second;
            } else if (!ec && size > 0 && size <= (4u << 20)) {
                std::ifstream in(*existing, std::ios::binary);
                auto bytes = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(size));
                if (in.read(reinterpret_cast<char*>(bytes->data()), static_cast<std::streamsize>(size))) {
                    const std::string name = lower(existing->filename().string());
                    for (const Process::FilePatch& patch : p.file_patches()) {
                        if (patch.file != name) continue;
                        auto at = std::search(bytes->begin(), bytes->end(), patch.from.begin(), patch.from.end());
                        for (; at != bytes->end(); at = std::search(at, bytes->end(), patch.from.begin(), patch.from.end()))
                            at = std::copy(patch.to.begin(), patch.to.end(), at);
                    }
                    if (st->contents_size + size > (512u << 20)) {
                        st->contents.clear();
                        st->contents_size = 0;
                    }
                    st->contents[existing->string()] = bytes;
                    st->contents_size += size;
                    file->data = std::move(bytes);
                }
            }
            if (!file->data) file->stream.open(*existing, std::ios::binary | std::ios::in);
        }
        if (!file->data && !file->stream) {
            p.last_error = kErrorFileNotFound;
            return kInvalidHandle;
        }
        p.last_error = 0;
        const uint32_t handle = st->next_handle += 4;
        st->files[handle] = std::move(file);
        return handle;
    });
    def("CloseHandle", 1, [&p, st] {
        st->files.erase(p.arg(0));
        return 1u;
    });
    def("ReadFile", 5, [&p, &cpu, st] {
        const auto found = st->files.find(p.arg(0));
        if (found == st->files.end()) {
            p.last_error = kErrorInvalidHandle;
            return 0u;
        }
        if (OpenFile& file = *found->second; file.data) {
            const size_t got = std::min<size_t>(p.arg(2), file.data->size() - std::min(file.position, file.data->size()));
            if (got) cpu.write(p.arg(1), file.data->data() + file.position, got);
            file.position += got;
            if (p.arg(3)) cpu.write_u32(p.arg(3), static_cast<uint32_t>(got));
            return 1u;
        }
        std::vector<char> buffer(p.arg(2));
        std::fstream& f = found->second->stream;
        f.clear();
        f.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const uint32_t got = static_cast<uint32_t>(f.gcount());
        f.clear();
        if (got) cpu.write(p.arg(1), buffer.data(), got);
        if (p.arg(3)) cpu.write_u32(p.arg(3), got);
        return 1u;
    });
    def("WriteFile", 5, [&p, &cpu, st] {
        const auto found = st->files.find(p.arg(0));
        if (found == st->files.end()) {
            p.last_error = kErrorInvalidHandle;
            return 0u;
        }
        if (found->second->data) {
            p.last_error = 5;  // ERROR_ACCESS_DENIED : fichier ouvert en lecture
            return 0u;
        }
        std::vector<char> buffer(p.arg(2));
        if (!buffer.empty()) cpu.read(p.arg(1), buffer.data(), buffer.size());
        std::fstream& f = found->second->stream;
        f.clear();
        f.seekp(f.tellg());
        f.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        f.seekg(f.tellp());
        if (p.arg(3)) cpu.write_u32(p.arg(3), f ? static_cast<uint32_t>(buffer.size()) : 0);
        return f ? 1u : 0u;
    });
    def("SetFilePointer", 4, [&p, st] {
        const auto found = st->files.find(p.arg(0));
        if (found == st->files.end()) {
            p.last_error = kErrorInvalidHandle;
            return kInvalidHandle;
        }
        if (OpenFile& file = *found->second; file.data) {
            const int64_t base = p.arg(3) == 0 ? 0 : p.arg(3) == 1 ? static_cast<int64_t>(file.position)
                                                                  : static_cast<int64_t>(file.data->size());
            const int64_t target = base + static_cast<int32_t>(p.arg(1));
            if (target < 0) return kInvalidHandle;
            file.position = static_cast<size_t>(target);
            return static_cast<uint32_t>(target);
        }
        std::fstream& f = found->second->stream;
        f.clear();
        const auto way = p.arg(3) == 0 ? std::ios::beg : p.arg(3) == 1 ? std::ios::cur : std::ios::end;
        f.seekg(static_cast<int32_t>(p.arg(1)), way);
        if (!f) {
            f.clear();
            return kInvalidHandle;
        }
        return static_cast<uint32_t>(f.tellg());
    });
    def("GetFileSize", 2, [&p, &cpu, st] {
        const auto found = st->files.find(p.arg(0));
        if (found == st->files.end()) return kInvalidHandle;
        if (!found->second->data) found->second->stream.flush();
        std::error_code ec;
        const uint64_t size = found->second->data ? found->second->data->size() : fs::file_size(found->second->path, ec);
        if (p.arg(1)) cpu.write_u32(p.arg(1), static_cast<uint32_t>(size >> 32));
        return static_cast<uint32_t>(size);
    });
    def("SetEndOfFile", 1, [&p, st] {
        const auto found = st->files.find(p.arg(0));
        if (found == st->files.end() || found->second->data) return 0u;
        std::fstream& f = found->second->stream;
        f.flush();
        std::error_code ec;
        fs::resize_file(found->second->path, static_cast<uintmax_t>(f.tellg()), ec);
        return ec ? 0u : 1u;
    });
    def("FlushFileBuffers", 1, [&p, st] {
        const auto found = st->files.find(p.arg(0));
        if (found != st->files.end() && !found->second->data) found->second->stream.flush();
        return 1u;
    });
    def("GetFileInformationByHandle", 2, [&p, &cpu, st] {
        const auto found = st->files.find(p.arg(0));
        if (found == st->files.end()) return 0u;
        if (!found->second->data) found->second->stream.flush();
        std::error_code ec;
        const uint64_t size = fs::file_size(found->second->path, ec);
        const uint64_t time = to_filetime(fs::last_write_time(found->second->path, ec));
        const uint32_t info = p.arg(1);
        cpu.fill(info, 0, 52);
        cpu.write_u32(info, 0x20);
        for (uint32_t offset : {4u, 12u, 20u}) {
            cpu.write_u32(info + offset, static_cast<uint32_t>(time));
            cpu.write_u32(info + offset + 4, static_cast<uint32_t>(time >> 32));
        }
        cpu.write_u32(info + 32, static_cast<uint32_t>(size >> 32));
        cpu.write_u32(info + 36, static_cast<uint32_t>(size));
        cpu.write_u32(info + 40, 1);
        return 1u;
    });
    def("GetFileAttributesA", 1, [&p, &cpu, st] {
        const auto found = st->vfs.find(cpu.read_string(p.arg(0)));
        if (!found) {
            p.last_error = kErrorFileNotFound;
            return kInvalidHandle;
        }
        return fs::is_directory(*found) ? 0x10u : 0x20u;
    });
    def("SetFileAttributesA", 2, [] { return 1u; });
    def("DeleteFileA", 1, [&p, &cpu, st] {
        if (st->vfs.remove(cpu.read_string(p.arg(0)))) return 1u;
        p.last_error = kErrorFileNotFound;
        return 0u;
    });
    def("CopyFileA", 3, [&p, &cpu, st] {
        const auto source = st->vfs.find(cpu.read_string(p.arg(0)));
        const std::string target_name = cpu.read_string(p.arg(1));
        if (!source || (p.arg(2) && st->vfs.find(target_name))) {
            p.last_error = source ? kErrorAlreadyExists : kErrorFileNotFound;
            return 0u;
        }
        const auto target = st->vfs.writable(target_name, false);
        std::error_code ec;
        if (!target || !fs::copy_file(*source, *target, fs::copy_options::overwrite_existing, ec)) return 0u;
        return 1u;
    });
    def("CreateDirectoryA", 2, [&p, &cpu, st] {
        if (st->vfs.make_directory(cpu.read_string(p.arg(0)))) return 1u;
        p.last_error = kErrorAlreadyExists;
        return 0u;
    });
    def("RemoveDirectoryA", 1, [&p, &cpu, st] { return st->vfs.remove(cpu.read_string(p.arg(0))) ? 1u : 0u; });
    def("GetCurrentDirectoryA", 2, [&p, &cpu, st] {
        const std::string& cwd = st->vfs.current_directory();
        if (cwd.size() + 1 > p.arg(0)) return static_cast<uint32_t>(cwd.size() + 1);
        cpu.write_string(p.arg(1), cwd);
        return static_cast<uint32_t>(cwd.size());
    });
    def("SetCurrentDirectoryA", 1, [&p, &cpu, st] {
        return st->vfs.set_current_directory(cpu.read_string(p.arg(0))) ? 1u : 0u;
    });
    def("GetFullPathNameA", 4, [&p, &cpu, st] {
        const std::string full = st->vfs.absolute(cpu.read_string(p.arg(0)));
        if (full.size() + 1 > p.arg(1)) return static_cast<uint32_t>(full.size() + 1);
        cpu.write_string(p.arg(2), full);
        if (p.arg(3)) cpu.write_u32(p.arg(3), p.arg(2) + static_cast<uint32_t>(full.find_last_of('\\') + 1));
        return static_cast<uint32_t>(full.size());
    });
    def("FindFirstFileA", 2, [&p, &cpu, st] {
        FindState find;
        find.entries = st->vfs.list(cpu.read_string(p.arg(0)));
        if (find.entries.empty()) {
            p.last_error = kErrorFileNotFound;
            return kInvalidHandle;
        }
        write_find_data(cpu, p.arg(1), find.entries[0]);
        find.next = 1;
        const uint32_t handle = st->next_handle += 4;
        st->finds[handle] = std::move(find);
        return handle;
    });
    def("FindNextFileA", 2, [&p, &cpu, st] {
        const auto found = st->finds.find(p.arg(0));
        if (found == st->finds.end() || found->second.next >= found->second.entries.size()) {
            p.last_error = kErrorNoMoreFiles;
            return 0u;
        }
        write_find_data(cpu, p.arg(1), found->second.entries[found->second.next++]);
        return 1u;
    });
    def("FindClose", 1, [&p, st] {
        st->finds.erase(p.arg(0));
        return 1u;
    });
    def("CompareFileTime", 2, [&p, &cpu] {
        const uint64_t a = cpu.read_u32(p.arg(0)) | static_cast<uint64_t>(cpu.read_u32(p.arg(0) + 4)) << 32;
        const uint64_t b = cpu.read_u32(p.arg(1)) | static_cast<uint64_t>(cpu.read_u32(p.arg(1) + 4)) << 32;
        return a < b ? 0xFFFFFFFFu : a == b ? 0u : 1u;
    });
    def("GetDriveTypeA", 1, [&p, &cpu] {
        const std::string root = p.arg(0) ? cpu.read_string(p.arg(0)) : std::string(kGuestDir);
        return !root.empty() && std::toupper(static_cast<unsigned char>(root[0])) == 'C' ? 3u : 1u;  // disque fixe, sinon absent
    });
    def("GetDiskFreeSpaceA", 5, [&p, &cpu] {
        const uint32_t values[4] = {8, 512, 2000000, 4000000};  // secteurs/cluster, octets/secteur, libres, total
        for (int i = 0; i < 4; ++i)
            if (p.arg(1 + i)) cpu.write_u32(p.arg(1 + i), values[i]);
        return 1u;
    });
    def("GetVolumeInformationA", 8, [&p, &cpu] {
        if (p.arg(1) && p.arg(2)) cpu.write_string(p.arg(1), "");
        if (p.arg(3)) cpu.write_u32(p.arg(3), 0x12345678);
        if (p.arg(4)) cpu.write_u32(p.arg(4), 255);
        if (p.arg(5)) cpu.write_u32(p.arg(5), 0);
        if (p.arg(6) && p.arg(7)) cpu.write_string(p.arg(6), "NTFS");
        return 1u;
    });
    def("GetPrivateProfileStringA", 6, [&p, &cpu, st] {
        const std::string section = p.arg(0) ? lower(cpu.read_string(p.arg(0))) : "";
        const std::string key = p.arg(1) ? lower(cpu.read_string(p.arg(1))) : "";
        std::string value = p.arg(2) ? cpu.read_string(p.arg(2)) : "";
        const uint32_t room = p.arg(4);
        if (const auto path = st->vfs.find(cpu.read_string(p.arg(5)))) {
            std::ifstream in(*path);
            std::string line, current;
            while (std::getline(in, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                if (line.size() > 2 && line.front() == '[' && line.back() == ']') {
                    current = lower(line.substr(1, line.size() - 2));
                } else if (current == section) {
                    const size_t eq = line.find('=');
                    if (eq != std::string::npos && lower(line.substr(0, eq)) == key) {
                        value = line.substr(eq + 1);
                        break;
                    }
                }
            }
        }
        if (room == 0) return 0u;
        value = value.substr(0, room - 1);
        cpu.write_string(p.arg(3), value);
        return static_cast<uint32_t>(value.size());
    });
}

}  // namespace zeus::win32::kernel32
