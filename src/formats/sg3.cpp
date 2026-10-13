#include "formats/sg3.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace zeus::formats {
namespace {

constexpr size_t kHeaderSize = 680;
constexpr size_t kBitmapRecords = 200, kBitmapSize = 200;
constexpr size_t kTagRecords = 300, kTagSize = 48;
constexpr int kTileW = 58, kTileH = 30;
constexpr size_t kTileBytes = 1800;  // 900 pixels de 16 bits
constexpr uint16_t kIsoKey = 0x03E0;  // vert pur : pixel de socle à ne pas dessiner

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
           static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}
int16_t i16(const uint8_t* p) { return static_cast<int16_t>(u16(p)); }
int32_t i32(const uint8_t* p) { return static_cast<int32_t>(u32(p)); }

std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) fail("lecture impossible : " + path.string());
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

std::string c_string(const uint8_t* p, size_t max) {
    size_t n = 0;
    while (n < max && p[n]) ++n;
    return std::string(reinterpret_cast<const char*>(p), n);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Écrit une couleur 15 bits (0RRRRRGGGGGBBBBB) au pixel d'indice pos.
void put(std::vector<uint8_t>& out, size_t pos, uint16_t c) {
    const uint8_t r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
    uint8_t* o = &out[pos * 4];
    o[0] = static_cast<uint8_t>(r << 3 | r >> 2);
    o[1] = static_cast<uint8_t>(g << 3 | g >> 2);
    o[2] = static_cast<uint8_t>(b << 3 | b >> 2);
    o[3] = 255;
}

void decode_plain(std::vector<uint8_t>& out, size_t total, const uint8_t* pix, size_t n) {
    if (n != total * 2) fail("taille incorrecte pour une image non compressée");
    for (size_t pos = 0; pos < total; ++pos) {
        const uint16_t c = u16(pix + pos * 2);
        if (c != 0xF81F) put(out, pos, c);  // couleur réservée à la transparence
    }
}

// Suites : 255,n = n pixels transparents ; sinon c = c pixels de 16 bits qui suivent.
void decode_runs(std::vector<uint8_t>& out, size_t total, const uint8_t* pix, size_t n) {
    size_t i = 0, pos = 0;
    while (i < n) {
        const uint8_t c = pix[i++];
        if (c == 255) {
            if (i >= n) fail("suite tronquée");
            pos += pix[i++];
        } else {
            if (i + 2 * static_cast<size_t>(c) > n || pos + c > total) fail("suite hors limites");
            for (uint8_t k = 0; k < c; ++k, i += 2) put(out, pos + k, u16(pix + i));
            pos += c;
        }
    }
    if (pos > total) fail("trop de pixels décodés");
}

// Masque alpha (version 0xD6) : mêmes suites, un octet d'opacité sur 5 bits par pixel.
void decode_alpha(std::vector<uint8_t>& out, size_t total, const uint8_t* data, size_t n) {
    size_t i = 0, pos = 0;
    while (i < n) {
        const uint8_t c = data[i++];
        if (c == 255) {
            if (i >= n) fail("masque alpha tronqué");
            pos += data[i++];
        } else {
            if (i + c > n || pos + c > total) fail("masque alpha hors limites");
            for (uint8_t k = 0; k < c; ++k) {
                const uint8_t a = data[i + k];
                if (a > 31) fail("opacité supérieure à 31");
                out[(pos + k) * 4 + 3] = static_cast<uint8_t>(a << 3 | a >> 2);
            }
            i += c;
            pos += c;
        }
    }
}

// Socle isométrique : losanges de 58x30 rangés par lignes, du haut vers le bas.
void decode_iso_base(std::vector<uint8_t>& out, int w, int h, const uint8_t* pix, size_t n,
                     const SgImage& im) {
    const int base_h = (w + 2) / 2;
    const int size = im.iso_size ? im.iso_size : base_h / kTileH;
    if (size * kTileH != base_h || static_cast<size_t>(w + 2) * base_h != n ||
        n != static_cast<size_t>(size) * size * kTileBytes)
        fail("socle isométrique incohérent");
    const int top = h - base_h;
    size_t i = 0;
    for (int row = 0; row < 2 * size - 1; ++row) {
        const int count = row < size ? row + 1 : 2 * size - row - 1;
        const int x0 = (row < size ? size - row - 1 : row - size + 1) * kTileH;
        const int y0 = top + row * kTileH / 2;
        for (int k = 0; k < count; ++k) {
            const int tx = x0 + k * (kTileW + 2);
            for (int y = 0; y < kTileH; ++y) {
                const int start = y < kTileH / 2 ? kTileH - 2 * (y + 1) : 2 * y - kTileH;
                for (int x = start; x < kTileW - start; ++x, i += 2) {
                    const uint16_t c = u16(pix + i);
                    // Couleur-clé : le socle des tuiles de relief laisse voir le dessous.
                    if ((c & 0x7FFF) != kIsoKey) put(out, static_cast<size_t>(y0 + y) * w + tx + x, c);
                }
            }
        }
    }
}

}  // namespace

SgFile::SgFile(const std::filesystem::path& path) : path_(path) {
    const std::vector<uint8_t> d = read_file(path);
    if (d.size() < kHeaderSize) fail("fichier .sg3 trop court : " + path.string());
    version_ = u32(&d[4]);
    if (version_ != 0xD5 && version_ != 0xD6) fail("version .sg3 inconnue : " + path.string());
    const size_t record = version_ == 0xD6 ? 72 : 64;
    const size_t max_images = u32(&d[12]), num_images = u32(&d[16]), num_bitmaps = u32(&d[20]);
    const size_t images_at = kHeaderSize + kBitmapRecords * kBitmapSize;
    if (d.size() != images_at + max_images * record + kTagRecords * kTagSize ||
        num_images >= max_images || num_bitmaps > kBitmapRecords)
        fail("structure .sg3 inattendue : " + path.string());

    for (size_t i = 0; i < num_bitmaps; ++i) {
        const uint8_t* p = &d[kHeaderSize + kBitmapSize * i];
        SgBitmap b;
        b.filename = c_string(p, 65);
        b.comment = c_string(p + 65, 51);
        b.width = u32(p + 116);
        b.height = u32(p + 120);
        b.num_images = u32(p + 124);
        b.start = u32(p + 128);
        b.end = u32(p + 132);
        bitmaps_.push_back(std::move(b));
    }
    for (size_t i = 0; i <= num_images; ++i) {
        const uint8_t* r = &d[images_at + record * i];
        SgImage im;
        im.offset = u32(r);
        im.length = u32(r + 4);
        im.uncompressed_length = u32(r + 8);
        im.invert_offset = i32(r + 16);
        im.width = i16(r + 20);
        im.height = i16(r + 22);
        im.num_animation_sprites = u16(r + 30);
        im.sprite_offset_x = i16(r + 34);
        im.sprite_offset_y = i16(r + 36);
        im.type = r[50];
        im.fully_compressed = r[51];
        im.external = r[52];
        im.has_compressed_part = r[53];
        im.iso_size = r[55];
        im.bitmap_id = r[56];
        im.alpha_length = record == 72 ? u32(r + 68) : 0;
        images_.push_back(im);
    }
}

const std::vector<uint8_t>& SgFile::pixel_file(const SgImage& image) {
    std::filesystem::path name;
    if (image.external) {
        if (image.bitmap_id >= bitmaps_.size()) fail("indice de bitmap hors limites");
        name = std::filesystem::path(bitmaps_[image.bitmap_id].filename).stem();
    } else {
        name = path_.stem();
    }
    const std::string key = lower(name.string() + ".555");
    auto found = pixel_files_.find(key);
    if (found != pixel_files_.end()) return found->second;
    // Les noms ne respectent pas la casse d'un fichier à l'autre : recherche insensible.
    for (const auto& entry : std::filesystem::directory_iterator(path_.parent_path())) {
        if (lower(entry.path().filename().string()) == key)
            return pixel_files_.emplace(key, read_file(entry.path())).first->second;
    }
    fail("fichier de pixels introuvable : " + key);
}

Pixels SgFile::decode(size_t index) {
    const SgImage& requested = images_.at(index);
    const bool mirrored = requested.invert_offset != 0;
    const SgImage& src =
        mirrored ? images_.at(static_cast<size_t>(static_cast<int64_t>(index) + requested.invert_offset))
                 : requested;
    Pixels result;
    const int w = src.width, h = src.height;
    if (w <= 0 || h <= 0) {
        if (src.length) fail("dimensions invalides pour une image non vide");
        return result;
    }
    result.width = w;
    result.height = h;
    const size_t total = static_cast<size_t>(w) * h;
    result.rgba.assign(total * 4, 0);
    if (src.length == 0) return result;

    const std::vector<uint8_t>& file = pixel_file(src);
    const size_t start = src.offset - (src.external ? 1 : 0);
    if (start + src.length + static_cast<size_t>(src.alpha_length) > file.size())
        fail("données d'image hors fichier");
    const uint8_t* pix = &file[start];

    if (src.type == 30) {
        if (src.uncompressed_length > src.length) fail("socle plus long que l'image");
        decode_iso_base(result.rgba, w, h, pix, src.uncompressed_length, src);
        if (src.length > src.uncompressed_length)
            decode_runs(result.rgba, total, pix + src.uncompressed_length,
                        src.length - src.uncompressed_length);
    } else if (src.fully_compressed) {
        decode_runs(result.rgba, total, pix, src.length);
    } else {
        decode_plain(result.rgba, total, pix, src.length);
    }
    if (src.alpha_length) decode_alpha(result.rgba, total, pix + src.length, src.alpha_length);

    if (mirrored) {
        for (int y = 0; y < h; ++y) {
            uint8_t* row = &result.rgba[static_cast<size_t>(y) * w * 4];
            for (int a = 0, b = w - 1; a < b; ++a, --b)
                std::swap_ranges(row + a * 4, row + a * 4 + 4, row + b * 4);
        }
    }
    return result;
}

}  // namespace zeus::formats
