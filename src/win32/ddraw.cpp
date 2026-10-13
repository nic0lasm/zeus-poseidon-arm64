// DDRAW : affichage du programme émulé.
//
// DirectDraw expose des objets COM : le programme reçoit un pointeur vers un objet dont le
// premier mot désigne une table de pointeurs de méthodes. Chaque méthode est ici une fonction
// hôte (convention stdcall, l'objet en premier argument). Une méthode non écrite arrête le
// programme en la nommant. Les surfaces vivent dans la mémoire émulée ; la surface primaire
// est recopiée dans la fenêtre à chaque fin d'écriture.

#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

#include "win32/process.h"

namespace zeus::win32 {
namespace {

constexpr uint32_t DD_OK = 0;
constexpr uint32_t DDERR_UNSUPPORTED = 0x80004001, DDERR_NOTFOUND = 0x887600FF;

// Indicateurs de DDSURFACEDESC et capacités de surface.
constexpr uint32_t DDSD_CAPS = 0x1, DDSD_HEIGHT = 0x2, DDSD_WIDTH = 0x4, DDSD_PITCH = 0x8,
                   DDSD_BACKBUFFERCOUNT = 0x20, DDSD_LPSURFACE = 0x800, DDSD_PIXELFORMAT = 0x1000;
constexpr uint32_t DDSCAPS_BACKBUFFER = 0x4, DDSCAPS_PRIMARYSURFACE = 0x200;
constexpr uint32_t DDBLT_COLORFILL = 0x400, DDBLT_KEYSRC = 0x8000;
constexpr uint32_t DDBLTFAST_SRCCOLORKEY = 0x1;
constexpr uint32_t WM_MOVE = 0x0003, WM_SIZE = 0x0005, WM_DISPLAYCHANGE = 0x007E;

struct Surface {
    int width = 0, height = 0, pitch = 0;
    uint32_t pixels = 0;      // adresse émulée des pixels (16 bits par pixel)
    bool primary = false;
    uint32_t back_buffer = 0; // objet de la surface arrière attachée, ou 0
    uint32_t color_key = 0;
    bool has_color_key = false;
    int refs = 1;
};

struct State {
    std::map<uint32_t, Surface> surfaces;  // adresse de l'objet -> surface
    uint32_t surface_vtable = 0, clipper_vtable = 0, palette_vtable = 0;
    int mode_width = 800, mode_height = 600;
    std::vector<uint8_t> frame;  // tampon hôte pour l'affichage
};

struct Method {
    const char* name;
    uint32_t args;  // en comptant l'objet
    std::function<uint32_t()> fn;  // vide : méthode non écrite
};

uint32_t make_vtable(Process& p, const std::string& interface, const std::vector<Method>& methods) {
    const uint32_t table = p.alloc(static_cast<uint32_t>(methods.size() * 4));
    for (size_t i = 0; i < methods.size(); ++i) {
        const std::string name = interface + "::" + methods[i].name;
        std::function<uint32_t()> fn = methods[i].fn;
        if (!fn) fn = []() -> uint32_t { throw std::runtime_error("méthode non fournie"); };
        p.cpu().write_u32(table + 4 * static_cast<uint32_t>(i), p.host_function(name, methods[i].args * 4, std::move(fn)));
    }
    return table;
}

uint32_t make_object(Process& p, uint32_t vtable) {
    const uint32_t object = p.alloc(8);
    p.cpu().write_u32(object, vtable);
    return object;
}

// Format 16 bits annoncé au programme : 5-5-5, celui des fichiers de sprites du jeu.
void write_pixel_format(Cpu& cpu, uint32_t address) {
    cpu.fill(address, 0, 32);
    cpu.write_u32(address + 0, 32);
    cpu.write_u32(address + 4, 0x40);  // DDPF_RGB
    cpu.write_u32(address + 12, 16);
    cpu.write_u32(address + 16, 0x7C00);
    cpu.write_u32(address + 20, 0x03E0);
    cpu.write_u32(address + 24, 0x001F);
}

void write_surface_desc(Cpu& cpu, uint32_t desc, const Surface& s, uint32_t pixels) {
    cpu.write_u32(desc + 4, DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_LPSURFACE | DDSD_PIXELFORMAT);
    cpu.write_u32(desc + 8, static_cast<uint32_t>(s.height));
    cpu.write_u32(desc + 12, static_cast<uint32_t>(s.width));
    cpu.write_u32(desc + 16, static_cast<uint32_t>(s.pitch));
    cpu.write_u32(desc + 36, pixels);
    write_pixel_format(cpu, desc + 72);
    cpu.write_u32(desc + 104, s.primary ? DDSCAPS_PRIMARYSURFACE : 0x40);  // sinon OFFSCREENPLAIN
}

struct Rect {
    int left, top, right, bottom;
};

Rect read_rect(Cpu& cpu, uint32_t address, const Surface& whole) {
    if (!address) return {0, 0, whole.width, whole.height};
    return {static_cast<int32_t>(cpu.read_u32(address)), static_cast<int32_t>(cpu.read_u32(address + 4)),
            static_cast<int32_t>(cpu.read_u32(address + 8)), static_cast<int32_t>(cpu.read_u32(address + 12))};
}

}  // namespace

void install_ddraw(Process& p) {
    auto st = std::make_shared<State>();
    Cpu& cpu = p.cpu();

    auto surface = [st](uint32_t object) -> Surface& {
        const auto found = st->surfaces.find(object);
        if (found == st->surfaces.end()) throw std::runtime_error("surface inconnue");
        return found->second;
    };
    // Recopie la surface primaire dans la fenêtre.
    auto present = [&p, &cpu, st](const Surface& s) {
        st->frame.resize(static_cast<size_t>(s.pitch) * s.height);
        cpu.read(s.pixels, st->frame.data(), st->frame.size());
        p.desktop().present(st->frame.data(), s.width, s.height, s.pitch, false);
        if (p.desktop().should_stop()) p.exit(0);
    };
    auto new_surface = [&p, st](int width, int height, bool primary) {
        Surface s;
        s.width = width;
        s.height = height;
        s.pitch = width * 2;
        s.pixels = p.alloc(static_cast<uint32_t>(s.pitch * height));
        s.primary = primary;
        const uint32_t object = make_object(p, st->surface_vtable);
        st->surfaces[object] = s;
        return object;
    };
    // Changement de mode d'affichage : la fenêtre plein écran suit la taille de l'écran et en
    // est avertie. Le jeu en dépend : son traitement de WM_MOVE (0x5C4C14) est le seul code qui
    // fixe le rectangle d'écran (0xF2AB08) copié à chaque image par FUN_0050c1b0.
    // Hypothèse : la liste et l'ordre exacts des messages de Windows ne sont pas établis.
    auto set_mode = [&p, st](int width, int height, uint32_t result) {
        Desktop& d = p.desktop();
        if (width == d.width() && height == d.height()) return;
        d.set_mode(width, height);
        const uint32_t size = static_cast<uint32_t>(height) << 16 | static_cast<uint32_t>(width);
        p.send_messages({{d.hwnd, WM_DISPLAYCHANGE, 16, size}, {d.hwnd, WM_SIZE, 0, size}, {d.hwnd, WM_MOVE, 0, 0}},
                        result);
    };
    // Copie d'un rectangle entre surfaces, avec couleur transparente facultative.
    auto copy = [&cpu](const Surface& dst, int dx, int dy, const Surface& src, Rect r, bool keyed) {
        if (r.left < 0) { dx -= r.left; r.left = 0; }
        if (r.top < 0) { dy -= r.top; r.top = 0; }
        r.right = std::min(r.right, src.width);
        r.bottom = std::min(r.bottom, src.height);
        if (dx < 0) { r.left -= dx; dx = 0; }
        if (dy < 0) { r.top -= dy; dy = 0; }
        const int w = std::min(r.right - r.left, dst.width - dx), h = std::min(r.bottom - r.top, dst.height - dy);
        if (w <= 0 || h <= 0) return;
        std::vector<uint8_t> line(static_cast<size_t>(w) * 2), under(keyed ? line.size() : 0);
        for (int y = 0; y < h; ++y) {
            const uint32_t from = src.pixels + static_cast<uint32_t>((r.top + y) * src.pitch + r.left * 2);
            const uint32_t to = dst.pixels + static_cast<uint32_t>((dy + y) * dst.pitch + dx * 2);
            cpu.read(from, line.data(), line.size());
            if (keyed) {
                cpu.read(to, under.data(), under.size());
                const uint8_t lo = static_cast<uint8_t>(src.color_key), hi = static_cast<uint8_t>(src.color_key >> 8);
                for (int x = 0; x < w; ++x)
                    if (line[2 * x] == lo && line[2 * x + 1] == hi) {
                        line[2 * x] = under[2 * x];
                        line[2 * x + 1] = under[2 * x + 1];
                    }
            }
            cpu.write(to, line.data(), line.size());
        }
    };

    // --- IDirectDrawSurface ------------------------------------------------------------
    st->surface_vtable = make_vtable(p, "IDirectDrawSurface", {
        {"QueryInterface", 3, [&p, &cpu, surface] {
            ++surface(p.arg(0)).refs;
            cpu.write_u32(p.arg(2), p.arg(0));
            return DD_OK;
        }},
        {"AddRef", 1, [&p, surface] { return static_cast<uint32_t>(++surface(p.arg(0)).refs); }},
        {"Release", 1, [&p, st] {
            const auto found = st->surfaces.find(p.arg(0));
            if (found == st->surfaces.end()) return 0u;
            if (--found->second.refs > 0) return static_cast<uint32_t>(found->second.refs);
            p.release(found->second.pixels);
            st->surfaces.erase(found);
            return 0u;
        }},
        {"AddAttachedSurface", 2, {}},
        {"AddOverlayDirtyRect", 2, {}},
        {"Blt", 6, [&p, &cpu, surface, copy, present] {
            Surface& dst = surface(p.arg(0));
            const uint32_t flags = p.arg(4);
            const Rect d = read_rect(cpu, p.arg(1), dst);
            if (p.trace)
                std::fprintf(stderr, "    Blt vers %#x (%d,%d)-(%d,%d) depuis %#x, indicateurs %#x\n", p.arg(0), d.left,
                             d.top, d.right, d.bottom, p.arg(2), flags);
            if (flags & DDBLT_COLORFILL) {
                const uint16_t color = static_cast<uint16_t>(cpu.read_u32(p.arg(5) + 80));  // DDBLTFX.dwFillColor
                const int x0 = std::max(d.left, 0), x1 = std::min(d.right, dst.width);
                if (x1 > x0) {
                    std::vector<uint8_t> line(static_cast<size_t>(x1 - x0) * 2);
                    for (size_t i = 0; i < line.size(); i += 2) {
                        line[i] = static_cast<uint8_t>(color);
                        line[i + 1] = static_cast<uint8_t>(color >> 8);
                    }
                    for (int y = std::max(d.top, 0); y < std::min(d.bottom, dst.height); ++y)
                        cpu.write(dst.pixels + static_cast<uint32_t>(y * dst.pitch + x0 * 2), line.data(), line.size());
                }
            } else if (p.arg(2)) {
                const Surface& src = surface(p.arg(2));
                const Rect s = read_rect(cpu, p.arg(3), src);
                if (s.right - s.left != d.right - d.left || s.bottom - s.top != d.bottom - d.top)
                    throw std::runtime_error("copie avec changement d'échelle non fournie");
                copy(dst, d.left, d.top, src, s, (flags & DDBLT_KEYSRC) && src.has_color_key);
            }
            if (dst.primary) present(dst);
            return DD_OK;
        }},
        {"BltBatch", 4, {}},
        {"BltFast", 6, [&p, &cpu, surface, copy, present] {
            Surface& dst = surface(p.arg(0));
            const Surface& src = surface(p.arg(3));
            if (p.trace) {
                const Rect r = read_rect(cpu, p.arg(4), src);
                std::fprintf(stderr, "    BltFast vers %#x en (%d,%d) depuis %#x (%d,%d)-(%d,%d), indicateurs %#x\n",
                             p.arg(0), static_cast<int32_t>(p.arg(1)), static_cast<int32_t>(p.arg(2)), p.arg(3), r.left,
                             r.top, r.right, r.bottom, p.arg(5));
            }
            copy(dst, static_cast<int32_t>(p.arg(1)), static_cast<int32_t>(p.arg(2)), src,
                 read_rect(cpu, p.arg(4), src), (p.arg(5) & DDBLTFAST_SRCCOLORKEY) && src.has_color_key);
            if (dst.primary) present(dst);
            return DD_OK;
        }},
        {"DeleteAttachedSurface", 3, {}},
        {"EnumAttachedSurfaces", 3, {}},
        {"EnumOverlayZOrders", 4, {}},
        {"Flip", 3, [&p, surface, present] {
            Surface& front = surface(p.arg(0));
            if (!front.back_buffer) return DDERR_UNSUPPORTED;
            std::swap(front.pixels, surface(front.back_buffer).pixels);
            present(front);
            return DD_OK;
        }},
        {"GetAttachedSurface", 3, [&p, &cpu, surface] {
            Surface& s = surface(p.arg(0));
            if (!s.back_buffer || !(cpu.read_u32(p.arg(1)) & DDSCAPS_BACKBUFFER)) return DDERR_NOTFOUND;
            ++surface(s.back_buffer).refs;
            cpu.write_u32(p.arg(2), s.back_buffer);
            return DD_OK;
        }},
        {"GetBltStatus", 2, [] { return DD_OK; }},
        {"GetCaps", 2, [&p, &cpu, surface] {
            cpu.write_u32(p.arg(1), surface(p.arg(0)).primary ? DDSCAPS_PRIMARYSURFACE : 0x40);
            return DD_OK;
        }},
        {"GetClipper", 2, {}},
        {"GetColorKey", 3, {}},
        {"GetDC", 2, {}},
        {"GetFlipStatus", 2, [] { return DD_OK; }},
        {"GetOverlayPosition", 3, {}},
        {"GetPalette", 2, {}},
        {"GetPixelFormat", 2, [&p, &cpu] {
            write_pixel_format(cpu, p.arg(1));
            return DD_OK;
        }},
        {"GetSurfaceDesc", 2, [&p, &cpu, surface] {
            const Surface& s = surface(p.arg(0));
            write_surface_desc(cpu, p.arg(1), s, s.pixels);
            return DD_OK;
        }},
        {"Initialize", 3, {}},
        {"IsLost", 1, [] { return DD_OK; }},
        {"Lock", 5, [&p, &cpu, surface] {
            const Surface& s = surface(p.arg(0));
            uint32_t pixels = s.pixels;
            if (p.arg(1)) {
                const Rect r = read_rect(cpu, p.arg(1), s);
                pixels += static_cast<uint32_t>(r.top * s.pitch + r.left * 2);
            }
            write_surface_desc(cpu, p.arg(2), s, pixels);
            return DD_OK;
        }},
        {"ReleaseDC", 2, {}},
        {"Restore", 1, [] { return DD_OK; }},
        {"SetClipper", 2, [] { return DD_OK; }},
        {"SetColorKey", 3, [&p, &cpu, surface] {
            Surface& s = surface(p.arg(0));
            s.has_color_key = p.arg(2) != 0;
            if (p.arg(2)) s.color_key = cpu.read_u32(p.arg(2));  // dwColorSpaceLowValue
            return DD_OK;
        }},
        {"SetOverlayPosition", 3, {}},
        {"SetPalette", 2, [] { return DD_OK; }},
        {"Unlock", 2, [&p, surface, present] {
            const Surface& s = surface(p.arg(0));
            if (s.primary) present(s);
            return DD_OK;
        }},
        {"UpdateOverlay", 6, {}},
        {"UpdateOverlayDisplay", 2, {}},
        {"UpdateOverlayZOrder", 3, {}},
    });

    // --- IDirectDrawClipper et IDirectDrawPalette : acceptés, sans effet -----------------
    st->clipper_vtable = make_vtable(p, "IDirectDrawClipper", {
        {"QueryInterface", 3, {}},
        {"AddRef", 1, [] { return 2u; }},
        {"Release", 1, [] { return 0u; }},
        {"GetClipList", 4, {}},
        {"GetHWnd", 2, {}},
        {"Initialize", 3, {}},
        {"IsClipListChanged", 2, {}},
        {"SetClipList", 3, [] { return DD_OK; }},
        {"SetHWnd", 3, [] { return DD_OK; }},
    });
    st->palette_vtable = make_vtable(p, "IDirectDrawPalette", {
        {"QueryInterface", 3, {}},
        {"AddRef", 1, [] { return 2u; }},
        {"Release", 1, [] { return 0u; }},
        {"GetCaps", 2, {}},
        {"GetEntries", 5, {}},
        {"Initialize", 4, {}},
        {"SetEntries", 5, [] { return DD_OK; }},
    });

    // --- IDirectDraw -------------------------------------------------------------------
    const uint32_t ddraw_vtable = make_vtable(p, "IDirectDraw", {
        {"QueryInterface", 3, [&p, &cpu] {
            uint8_t iid[16];
            cpu.read(p.arg(1), iid, 16);
            std::fprintf(stderr, "[hôte] IDirectDraw::QueryInterface {%02x%02x%02x%02x-...} : même objet rendu\n",
                         iid[3], iid[2], iid[1], iid[0]);
            cpu.write_u32(p.arg(2), p.arg(0));
            return DD_OK;
        }},
        {"AddRef", 1, [] { return 2u; }},
        {"Release", 1, [] { return 0u; }},
        {"Compact", 1, [] { return DD_OK; }},
        {"CreateClipper", 4, [&p, &cpu, st] {
            cpu.write_u32(p.arg(2), make_object(p, st->clipper_vtable));
            return DD_OK;
        }},
        {"CreatePalette", 5, [&p, &cpu, st] {
            cpu.write_u32(p.arg(3), make_object(p, st->palette_vtable));
            return DD_OK;
        }},
        {"CreateSurface", 4, [&p, &cpu, st, new_surface, surface] {
            const uint32_t desc = p.arg(1);
            const uint32_t flags = cpu.read_u32(desc + 4), caps = cpu.read_u32(desc + 104);
            const bool primary = (flags & DDSD_CAPS) && (caps & DDSCAPS_PRIMARYSURFACE);
            int width = st->mode_width, height = st->mode_height;
            if (!primary) {
                if (flags & DDSD_WIDTH) width = static_cast<int32_t>(cpu.read_u32(desc + 12));
                if (flags & DDSD_HEIGHT) height = static_cast<int32_t>(cpu.read_u32(desc + 8));
            }
            if (width <= 0 || height <= 0 || width > 8192 || height > 8192) return 0x80070057u;  // E_INVALIDARG
            const uint32_t object = new_surface(width, height, primary);
            if (p.trace)
                std::fprintf(stderr, "    surface %#x : %dx%d%s (indicateurs %#x, capacités %#x)\n", object, width,
                             height, primary ? ", primaire" : "", flags, caps);
            if (primary && (flags & DDSD_BACKBUFFERCOUNT) && cpu.read_u32(desc + 20) > 0)
                surface(object).back_buffer = new_surface(width, height, false);
            cpu.write_u32(p.arg(2), object);
            return DD_OK;
        }},
        {"DuplicateSurface", 3, {}},
        {"EnumDisplayModes", 5, {}},
        {"EnumSurfaces", 5, {}},
        {"FlipToGDISurface", 1, [] { return DD_OK; }},
        {"GetCaps", 3, [&p, &cpu] {
            // Capacités toutes nulles : pas d'accélération matérielle annoncée.
            for (int i = 1; i <= 2; ++i) {
                const uint32_t caps = p.arg(i);
                if (!caps) continue;
                const uint32_t size = cpu.read_u32(caps);
                if (size > 4 && size <= 1024) cpu.fill(caps + 4, 0, size - 4);
            }
            return DD_OK;
        }},
        {"GetDisplayMode", 2, [&p, &cpu, st] {
            Surface mode;
            mode.width = st->mode_width;
            mode.height = st->mode_height;
            mode.pitch = st->mode_width * 2;
            write_surface_desc(cpu, p.arg(1), mode, 0);
            cpu.write_u32(p.arg(1) + 4, DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT);
            return DD_OK;
        }},
        {"GetFourCCCodes", 3, {}},
        {"GetGDISurface", 2, {}},
        {"GetMonitorFrequency", 2, [&p, &cpu] {
            cpu.write_u32(p.arg(1), 60);
            return DD_OK;
        }},
        {"GetScanLine", 2, {}},
        {"GetVerticalBlankStatus", 2, [&p, &cpu] {
            cpu.write_u32(p.arg(1), 1);
            return DD_OK;
        }},
        {"Initialize", 2, {}},
        {"RestoreDisplayMode", 1, [set_mode] {
            set_mode(Desktop::kScreenWidth, Desktop::kScreenHeight, DD_OK);
            return DD_OK;
        }},
        {"SetCooperativeLevel", 3, [] { return DD_OK; }},
        {"SetDisplayMode", 4, [&p, st, set_mode] {
            if (p.arg(3) != 16) return DDERR_UNSUPPORTED;
            st->mode_width = static_cast<int32_t>(p.arg(1));
            st->mode_height = static_cast<int32_t>(p.arg(2));
            if (p.trace) std::fprintf(stderr, "    mode %dx%d\n", st->mode_width, st->mode_height);
            set_mode(st->mode_width, st->mode_height, DD_OK);
            return DD_OK;
        }},
        {"WaitForVerticalBlank", 3, [] { return DD_OK; }},
    });

    p.def("ddraw.dll", "DirectDrawCreate", 12, [&p, &cpu, ddraw_vtable] {
        cpu.write_u32(p.arg(1), make_object(p, ddraw_vtable));
        return DD_OK;
    });
}

}  // namespace zeus::win32
