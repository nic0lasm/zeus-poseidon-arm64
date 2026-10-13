#include "win32/text.h"

#include <cstdint>

namespace zeus::win32 {
namespace {

// Octets 0x80 à 0x9F de Windows-1252 ; le reste coïncide avec Latin-1. 0 : octet non attribué.
constexpr uint16_t kHigh[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
    0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178};

uint32_t code_point(uint8_t byte) {
    if (byte >= 0x80 && byte < 0xA0) return kHigh[byte - 0x80] ? kHigh[byte - 0x80] : byte;
    return byte;
}

}  // namespace

std::string to_utf8(const std::string& windows_text) {
    std::string out;
    out.reserve(windows_text.size());
    for (unsigned char byte : windows_text) {
        const uint32_t c = code_point(byte);
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | c >> 6);
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | c >> 12);
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

std::string from_utf8(const std::string& utf8_text) {
    std::string out;
    out.reserve(utf8_text.size());
    for (size_t i = 0; i < utf8_text.size();) {
        const uint8_t lead = static_cast<uint8_t>(utf8_text[i]);
        const size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        uint32_t c = length == 1 ? lead : lead & (0xFF >> (length + 1));
        for (size_t k = 1; k < length && i + k < utf8_text.size(); ++k)
            c = c << 6 | (static_cast<uint8_t>(utf8_text[i + k]) & 0x3F);
        i += length;
        char byte = '?';
        if (c < 0x80 || (c >= 0xA0 && c < 0x100)) {
            byte = static_cast<char>(c);
        } else {
            for (int k = 0; k < 32; ++k)
                if (kHigh[k] == c) byte = static_cast<char>(0x80 + k);
        }
        out += byte;
    }
    return out;
}

}  // namespace zeus::win32
