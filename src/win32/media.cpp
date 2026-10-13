// IJL10 : la bibliothèque JPEG d'Intel, réalisée avec ImageIO de macOS.

#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "win32/process.h"

namespace zeus::win32 {
namespace {

struct Jpeg {
    int width = 0, height = 0;
    std::vector<uint8_t> rgb;  // 3 octets par pixel, de haut en bas
};

// Décode un JPEG ; `with_pixels` à faux ne lit que les dimensions.
bool decode_jpeg(const std::vector<uint8_t>& data, bool with_pixels, Jpeg& out) {
    CFDataRef cf = CFDataCreate(nullptr, data.data(), static_cast<CFIndex>(data.size()));
    CGImageSourceRef source = cf ? CGImageSourceCreateWithData(cf, nullptr) : nullptr;
    CGImageRef image = source ? CGImageSourceCreateImageAtIndex(source, 0, nullptr) : nullptr;
    bool ok = false;
    if (image) {
        out.width = static_cast<int>(CGImageGetWidth(image));
        out.height = static_cast<int>(CGImageGetHeight(image));
        ok = out.width > 0 && out.height > 0;
        if (ok && with_pixels) {
            std::vector<uint8_t> rgba(static_cast<size_t>(out.width) * out.height * 4);
            CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
            CGContextRef context = CGBitmapContextCreate(rgba.data(), static_cast<size_t>(out.width),
                                                         static_cast<size_t>(out.height), 8,
                                                         static_cast<size_t>(out.width) * 4, space,
                                                         static_cast<uint32_t>(kCGImageAlphaNoneSkipLast));
            if (context) {
                CGContextSetInterpolationQuality(context, kCGInterpolationNone);
                CGContextDrawImage(context, CGRectMake(0, 0, out.width, out.height), image);
                CGContextRelease(context);
                out.rgb.resize(static_cast<size_t>(out.width) * out.height * 3);
                for (size_t i = 0, n = static_cast<size_t>(out.width) * out.height; i < n; ++i) {
                    out.rgb[3 * i] = rgba[4 * i];
                    out.rgb[3 * i + 1] = rgba[4 * i + 1];
                    out.rgb[3 * i + 2] = rgba[4 * i + 2];
                }
            } else {
                ok = false;
            }
            CGColorSpaceRelease(space);
        }
        CGImageRelease(image);
    }
    if (source) CFRelease(source);
    if (cf) CFRelease(cf);
    return ok;
}

// Champs utilisés de JPEG_CORE_PROPERTIES (Intel JPEG Library 1.x).
constexpr uint32_t kDibBytes = 4, kDibWidth = 8, kDibHeight = 12, kDibPadBytes = 16, kDibChannels = 20,
                   kDibColor = 24, kJpgFile = 32, kJpgBytes = 36, kJpgSizeBytes = 40, kJpgWidth = 44,
                   kJpgHeight = 48, kJpgChannels = 52, kJpgColor = 56;
constexpr uint32_t kIjlOk = 0, kIjlError = 0xFFFFFFFF;  // IJL_OK, erreur générique (-1)

}  // namespace

void install_media(Process& p) {
    Cpu& cpu = p.cpu();

    p.def("ijl10.dll", "#2", 4, [] { return kIjlOk; });  // ijlInit
    p.def("ijl10.dll", "#3", 4, [] { return kIjlOk; });  // ijlFree
    p.def("ijl10.dll", "#4", 8, [&p, &cpu] {             // ijlRead(propriétés, opération)
        const uint32_t props = p.arg(0), operation = p.arg(1);
        // 0/2 : paramètres/image depuis un fichier ; 1/3 : depuis un tampon.
        if (operation > 3) throw std::runtime_error("opération IJL non fournie : " + std::to_string(operation));
        std::vector<uint8_t> data;
        if (operation == 0 || operation == 2) {
            const std::string name = cpu.read_string(cpu.read_u32(props + kJpgFile));
            const auto path = p.vfs().find(name);
            if (!path) return kIjlError;
            std::ifstream in(*path, std::ios::binary);
            data.assign(std::istreambuf_iterator<char>(in), {});
        } else {
            data.resize(cpu.read_u32(props + kJpgSizeBytes));
            if (!data.empty()) cpu.read(cpu.read_u32(props + kJpgBytes), data.data(), data.size());
        }
        const bool whole = operation >= 2;
        Jpeg jpeg;
        if (!decode_jpeg(data, whole, jpeg)) return kIjlError;
        cpu.write_u32(props + kJpgWidth, static_cast<uint32_t>(jpeg.width));
        cpu.write_u32(props + kJpgHeight, static_cast<uint32_t>(jpeg.height));
        cpu.write_u32(props + kJpgChannels, 3);
        cpu.write_u32(props + kJpgColor, 3);  // IJL_YCBCR
        if (!whole) return kIjlOk;

        const uint32_t dib = cpu.read_u32(props + kDibBytes);
        if (p.trace)
            std::fprintf(stderr, "    JPEG %dx%d -> tampon %#x, %d x %d, %u canaux, couleur %u, marge %u\n", jpeg.width,
                         jpeg.height, dib, static_cast<int32_t>(cpu.read_u32(props + kDibWidth)),
                         static_cast<int32_t>(cpu.read_u32(props + kDibHeight)), cpu.read_u32(props + kDibChannels),
                         cpu.read_u32(props + kDibColor), cpu.read_u32(props + kDibPadBytes));
        const int width = static_cast<int32_t>(cpu.read_u32(props + kDibWidth));
        const int signed_height = static_cast<int32_t>(cpu.read_u32(props + kDibHeight));
        const int height = signed_height < 0 ? -signed_height : signed_height;
        const uint32_t channels = cpu.read_u32(props + kDibChannels), pad = cpu.read_u32(props + kDibPadBytes);
        const bool bgr = cpu.read_u32(props + kDibColor) == 2;  // IJL_BGR
        if (channels != 3 || width != jpeg.width || height != jpeg.height)
            throw std::runtime_error("format de sortie IJL non fourni");
        const size_t stride = static_cast<size_t>(width) * 3 + pad;
        std::vector<uint8_t> line(stride, 0);
        for (int y = 0; y < height; ++y) {
            // Une hauteur négative demande une image de bas en haut.
            const int source_y = signed_height < 0 ? height - 1 - y : y;
            const uint8_t* src = &jpeg.rgb[static_cast<size_t>(source_y) * width * 3];
            for (int x = 0; x < width; ++x) {
                line[3 * x] = src[3 * x + (bgr ? 2 : 0)];
                line[3 * x + 1] = src[3 * x + 1];
                line[3 * x + 2] = src[3 * x + (bgr ? 0 : 2)];
            }
            cpu.write(dib + static_cast<uint32_t>(static_cast<size_t>(y) * stride), line.data(), stride);
        }
        return kIjlOk;
    });
}

}  // namespace zeus::win32
