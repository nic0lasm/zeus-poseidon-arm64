#include "formats/mapfile.h"

#include <fstream>
#include <stdexcept>
#include <string>

#include "formats/pkware.h"

namespace zeus::formats {
namespace {

constexpr size_t kTableCapacity = 300, kEntrySize = 20;
constexpr uint32_t kStoredRaw = 0x80000000;  // bloc compressible stocké tel quel

uint32_t u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
           static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

}  // namespace

MapFile::MapFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) fail("lecture impossible : " + path.string());
    const std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), {});
    if (d.size() < 12 + kTableCapacity * kEntrySize || d[0] != 'M' || d[1] != 'A' || d[2] != 'P' ||
        d[3] != 'S')
        fail("signature MAPS absente : " + path.string());
    version_ = u32(&d[4]);
    const size_t num_chunks = u32(&d[8]);
    if (num_chunks < 4 || num_chunks > kTableCapacity) fail("nombre de blocs invraisemblable");

    size_t pos = 0;
    for (size_t i = 0; i < num_chunks; ++i) {
        const uint8_t* e = &d[12 + kEntrySize * i];
        MapChunk c;
        c.compressed = u32(e);
        c.address = u32(e + 4);
        c.element_size = u32(e + 8);
        c.count = u32(e + 12);
        c.extra = u32(e + 16);
        const size_t size = static_cast<size_t>(c.element_size) * c.count;
        if (c.compressed == 0) {
            if (pos + size > d.size()) fail("bloc brut hors fichier");
            c.data.assign(d.begin() + pos, d.begin() + pos + size);
            pos += size;
        } else if (c.compressed == 1) {
            if (pos + 4 > d.size()) fail("longueur de bloc hors fichier");
            const uint32_t length = u32(&d[pos]);
            pos += 4;
            if (length == kStoredRaw) {
                if (pos + size > d.size()) fail("bloc brut hors fichier");
                c.data.assign(d.begin() + pos, d.begin() + pos + size);
                pos += size;
            } else {
                if (pos + length > d.size()) fail("bloc compressé hors fichier");
                c.data = explode(&d[pos], length, size);
                pos += length;
            }
        } else {
            fail("indicateur de compression inconnu");
        }
        chunks_.push_back(std::move(c));
    }
    if (pos != d.size()) fail("octets restants après le dernier bloc : " + path.string());
}

const MapChunk& MapFile::chunk(size_t index, uint32_t element_size, uint32_t count) const {
    if (index >= chunks_.size()) fail("bloc absent de la carte");
    const MapChunk& c = chunks_[index];
    if (c.element_size != element_size || c.count != count)
        fail("bloc " + std::to_string(index) + " de forme inattendue");
    return c;
}

}  // namespace zeus::formats
