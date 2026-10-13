// Lecture des sprites du jeu : index .sg3 + pixels .555 (voir formats/sg3.md).
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace zeus::formats {

struct SgBitmap {
    std::string filename;
    std::string comment;
    uint32_t width = 0, height = 0, num_images = 0, start = 0, end = 0;
};

struct SgImage {
    uint32_t offset = 0, length = 0, uncompressed_length = 0;
    int32_t invert_offset = 0;
    int16_t width = 0, height = 0;
    uint16_t num_animation_sprites = 0;
    int16_t sprite_offset_x = 0, sprite_offset_y = 0;
    uint8_t type = 0, fully_compressed = 0, external = 0, has_compressed_part = 0;
    uint8_t iso_size = 0, bitmap_id = 0;
    uint32_t alpha_length = 0;
};

// Image décodée, 4 octets par pixel dans l'ordre R, G, B, A, origine en haut à gauche.
struct Pixels {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
};

class SgFile {
public:
    explicit SgFile(const std::filesystem::path& path);

    uint32_t version() const { return version_; }
    // L'image 0 est un enregistrement vide ; les images utiles vont de 1 à image_count().
    size_t image_count() const { return images_.size() - 1; }
    const SgImage& image(size_t index) const { return images_.at(index); }
    const std::vector<SgBitmap>& bitmaps() const { return bitmaps_; }
    const std::filesystem::path& path() const { return path_; }

    // Partie d'une image isométrique : le jeu dessine tous les socles de la carte, puis toutes
    // les parties hautes. Pour une image non isométrique, tout est dans le socle.
    enum class Part { Whole, Footprint, Top };

    // Lève std::runtime_error si les données sont absentes ou incohérentes.
    Pixels decode(size_t index, Part part = Part::Whole);
    // Vrai si l'image a une partie haute distincte du socle.
    bool has_top(size_t index) const;

private:
    const std::vector<uint8_t>& pixel_file(const SgImage& image);

    std::filesystem::path path_;
    uint32_t version_ = 0;
    std::vector<SgBitmap> bitmaps_;
    std::vector<SgImage> images_;
    std::map<std::string, std::vector<uint8_t>> pixel_files_;
};

}  // namespace zeus::formats
