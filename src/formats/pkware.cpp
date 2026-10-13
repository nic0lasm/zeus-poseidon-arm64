#include "formats/pkware.h"

#include <array>
#include <stdexcept>

// Décompression PKWARE DCL « implode », d'après l'algorithme de blast.c (Mark Adler).

namespace zeus::formats {
namespace {

constexpr int kMaxBits = 13;

struct Huffman {
    std::array<uint16_t, kMaxBits + 1> count{};
    std::vector<uint16_t> symbol;
};

// Table canonique à partir de la forme compacte : 4 bits de longueur, 4 bits de répétitions - 1.
Huffman construct(const std::vector<uint8_t>& rep) {
    std::vector<uint8_t> lengths;
    for (uint8_t b : rep) lengths.insert(lengths.end(), (b >> 4) + 1, b & 15);
    Huffman h;
    for (uint8_t n : lengths) ++h.count[n];
    std::array<uint16_t, kMaxBits + 2> offs{};
    for (int n = 1; n <= kMaxBits; ++n) offs[n + 1] = static_cast<uint16_t>(offs[n] + h.count[n]);
    h.symbol.assign(lengths.size(), 0);
    for (size_t sym = 0; sym < lengths.size(); ++sym)
        if (lengths[sym]) h.symbol[offs[lengths[sym]]++] = static_cast<uint16_t>(sym);
    return h;
}

const Huffman& len_code() {
    static const Huffman h = construct({2, 35, 36, 53, 38, 23});
    return h;
}
const Huffman& dist_code() {
    static const Huffman h = construct({2, 20, 53, 230, 247, 151, 248});
    return h;
}

constexpr int kBase[16] = {3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264};
constexpr int kExtra[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8};

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    uint32_t bits(int need) {
        while (count_ < need) {
            if (pos_ >= size_) throw std::runtime_error("flux implode tronqué");
            buffer_ |= static_cast<uint32_t>(data_[pos_++]) << count_;
            count_ += 8;
        }
        const uint32_t value = buffer_ & ((1u << need) - 1);
        buffer_ >>= need;
        count_ -= need;
        return value;
    }

    int decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int length = 1; length <= kMaxBits; ++length) {
            code |= static_cast<int>(bits(1)) ^ 1;  // les codes sont stockés inversés
            const int count = h.count[length];
            if (code < first + count) return h.symbol[index + (code - first)];
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        throw std::runtime_error("code de Huffman invalide");
    }

private:
    const uint8_t* data_;
    size_t size_, pos_ = 0;
    uint32_t buffer_ = 0;
    int count_ = 0;
};

}  // namespace

std::vector<uint8_t> explode(const uint8_t* data, size_t size, size_t expected) {
    if (size < 2 || data[0] != 0 || data[1] < 4 || data[1] > 6)
        throw std::runtime_error("en-tête implode inattendu");
    const int dict_bits = data[1];
    BitReader in(data + 2, size - 2);
    std::vector<uint8_t> out;
    out.reserve(expected);
    for (;;) {
        if (in.bits(1)) {
            const int sym = in.decode(len_code());
            const int length = kBase[sym] + static_cast<int>(in.bits(kExtra[sym]));
            if (length == 519) break;
            const int low = length == 2 ? 2 : dict_bits;
            const size_t dist =
                (static_cast<size_t>(in.decode(dist_code())) << low) + in.bits(low) + 1;
            if (dist > out.size()) throw std::runtime_error("distance hors du texte déjà décodé");
            const size_t start = out.size() - dist;
            for (int k = 0; k < length; ++k) out.push_back(out[start + k]);  // recouvrement possible
        } else {
            out.push_back(static_cast<uint8_t>(in.bits(8)));
        }
        if (out.size() > expected) throw std::runtime_error("bloc plus long qu'annoncé");
    }
    if (out.size() != expected) throw std::runtime_error("bloc plus court qu'annoncé");
    return out;
}

}  // namespace zeus::formats
