// Décompression PKWARE DCL « implode » (mode binaire), utilisée par les cartes et sauvegardes.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace zeus::formats {

// Décompresse `size` octets ; le résultat doit faire exactement `expected` octets.
// Lève std::runtime_error si le flux est invalide.
std::vector<uint8_t> explode(const uint8_t* data, size_t size, size_t expected);

}  // namespace zeus::formats
