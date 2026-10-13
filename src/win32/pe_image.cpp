#include "win32/pe_image.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace zeus::win32 {
namespace {

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

class Reader {
public:
    explicit Reader(std::vector<uint8_t> data) : d_(std::move(data)) {}
    size_t size() const { return d_.size(); }
    const uint8_t* at(size_t offset, size_t length) const {
        if (offset + length > d_.size()) fail("lecture hors du fichier PE");
        return &d_[offset];
    }
    uint16_t u16(size_t o) const {
        const uint8_t* p = at(o, 2);
        return static_cast<uint16_t>(p[0] | p[1] << 8);
    }
    uint32_t u32(size_t o) const {
        const uint8_t* p = at(o, 4);
        return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
               static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
    }

private:
    std::vector<uint8_t> d_;
};

std::string cstring(const Cpu& cpu, uint32_t address) {
    std::string s;
    for (;; ++address) {
        char c = 0;
        cpu.read(address, &c, 1);
        if (!c || s.size() > 512) break;
        s.push_back(c);
    }
    return s;
}

}  // namespace

PeImage load_pe(const std::filesystem::path& path, Cpu& cpu) {
    std::ifstream in(path, std::ios::binary);
    if (!in) fail("lecture impossible : " + path.string());
    const Reader f(std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {}));
    if (f.size() < 0x40 || f.u16(0) != 0x5A4D) fail("pas un exécutable MZ");
    const size_t pe = f.u32(0x3C);
    if (f.u32(pe) != 0x00004550) fail("pas un exécutable PE");
    if (f.u16(pe + 4) != 0x014C) fail("exécutable non i386");
    const size_t section_count = f.u16(pe + 6);
    const size_t optional = pe + 24;
    if (f.u16(optional) != 0x010B) fail("exécutable non PE32");

    PeImage image;
    image.entry_point = f.u32(optional + 16);
    image.image_base = f.u32(optional + 28);
    image.image_size = f.u32(optional + 56);
    const uint32_t header_size = f.u32(optional + 60);
    const uint32_t import_rva = f.u32(optional + 96 + 8);
    image.entry_point += image.image_base;

    cpu.map(image.image_base, image.image_size);
    cpu.write(image.image_base, f.at(0, header_size), header_size);
    const size_t table = optional + f.u16(pe + 20);
    for (size_t i = 0; i < section_count; ++i) {
        const size_t s = table + 40 * i;
        PeSection section;
        const uint8_t* name = f.at(s, 8);
        for (int k = 0; k < 8 && name[k]; ++k) section.name.push_back(static_cast<char>(name[k]));
        section.virtual_size = f.u32(s + 8);
        section.address = image.image_base + f.u32(s + 12);
        section.raw_size = f.u32(s + 16);
        const uint32_t raw_offset = f.u32(s + 20);
        const uint32_t copied = std::min(section.raw_size, std::max(section.virtual_size, section.raw_size));
        if (copied) cpu.write(section.address, f.at(raw_offset, copied), copied);
        image.sections.push_back(section);
    }

    // Répertoire d'import : une entrée de 20 octets par DLL, terminée par une entrée nulle.
    for (uint32_t d = image.image_base + import_rva; import_rva != 0; d += 20) {
        const uint32_t lookup = cpu.read_u32(d), name = cpu.read_u32(d + 12), slots = cpu.read_u32(d + 16);
        if (!name) break;
        std::string dll = cstring(cpu, image.image_base + name);
        std::transform(dll.begin(), dll.end(), dll.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const uint32_t names = image.image_base + (lookup ? lookup : slots);
        for (uint32_t i = 0;; ++i) {
            const uint32_t v = cpu.read_u32(names + 4 * i);
            if (!v) break;
            PeImport imp;
            imp.dll = dll;
            imp.function = (v & 0x80000000) ? "#" + std::to_string(v & 0xFFFF)
                                            : cstring(cpu, image.image_base + v + 2);
            imp.slot = image.image_base + slots + 4 * i;
            image.imports.push_back(std::move(imp));
        }
    }
    return image;
}

}  // namespace zeus::win32
