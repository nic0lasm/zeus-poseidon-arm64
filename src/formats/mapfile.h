// Conteneur « MAPS » des cartes du jeu (voir formats/map.md).
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace zeus::formats {

// Un bloc correspond à une variable du jeu. Son adresse est celle de l'exécutable qui a
// enregistré le fichier : elle change d'une version à l'autre, alors que l'ordre des blocs est
// le même dans toutes les cartes livrées. Un bloc s'identifie donc par son rang.
struct MapChunk {
    uint32_t compressed = 0, address = 0, element_size = 0, count = 0, extra = 0;
    std::vector<uint8_t> data;  // contenu décompressé, element_size * count octets
};

class MapFile {
public:
    explicit MapFile(const std::filesystem::path& path);

    uint32_t version() const { return version_; }
    const std::vector<MapChunk>& chunks() const { return chunks_; }
    // Bloc de rang `index`, dont on exige la forme ; lève std::runtime_error sinon.
    const MapChunk& chunk(size_t index, uint32_t element_size, uint32_t count) const;

private:
    uint32_t version_ = 0;
    std::vector<MapChunk> chunks_;
};

}  // namespace zeus::formats
