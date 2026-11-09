// USER32, GDI32, WINMM, OLE32, SHELL32 : fenêtre, messages, souris et clavier du programme émulé.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <thread>

#include "win32/game_gog.h"
#include "win32/process.h"
#include "win32/text.h"

namespace zeus::win32 {
namespace {

constexpr uint32_t WM_CREATE = 0x0001, WM_DESTROY = 0x0002, WM_MOVE = 0x0003, WM_SIZE = 0x0005,
                   WM_ACTIVATE = 0x0006, WM_SETFOCUS = 0x0007, WM_PAINT = 0x000F, WM_CLOSE = 0x0010,
                   WM_QUIT = 0x0012, WM_SHOWWINDOW = 0x0018, WM_ACTIVATEAPP = 0x001C;
constexpr uint32_t WS_VISIBLE = 0x10000000;
constexpr int32_t GWL_STYLE = -16, GWL_EXSTYLE = -20;

struct State {
    std::map<std::string, uint32_t> classes;  // nom de classe -> procédure de fenêtre
    std::map<int32_t, uint32_t> window_longs;
    bool shown = false;
    int cursor_count = 0;
    uint32_t cursor = 0x00030001;             // handle given to the last SetCursor (first the arrow)
    bool cursor_set = false;                  // SetCursor already called
    std::map<uint32_t, uint32_t> cursors;     // resource number -> handle returned by LoadCursorA
    bool log_cursors = std::getenv("ZEUS_CURSOR_LOG") != nullptr;  // diagnostics: traces the cursors
    uint32_t next_message_id = 0xC000;
    uint32_t last_idle_poll = 0;  // instant de la dernière interrogation d'une file vide
    std::optional<bool> knows_turns;  // vrai si le code de FUN_005168a0 est celui attendu
};

// Instant où le jeu fera son prochain tour de simulation, d'après FUN_005168a0 : un tour est dû
// quand le temps écoulé depuis le dernier (gog::kLastTurn, horloge de timeGetTime) atteint
// l'intervalle de la vitesse du jeu (pourcentage en gog::kGameSpeed ; 0, 20, 35… ms en
// gog::kSpeedIntervals, de 100 % à 10 %) plus 2 ms. Rien si la vitesse sort de la table.
std::optional<uint32_t> next_simulation_turn(Cpu& cpu) {
    const int32_t speed = static_cast<int32_t>(cpu.read_u32(gog::kGameSpeed));
    const int32_t index = std::max((100 - speed) / 10, 0);
    if (index >= 10) return std::nullopt;
    return cpu.read_u32(gog::kLastTurn) + cpu.read_u32(gog::kSpeedIntervals + 4 * static_cast<uint32_t>(index)) + 2;
}

void write_message(Cpu& cpu, uint32_t address, const Message& m) {
    const uint32_t fields[7] = {m.hwnd, m.message, m.wparam, m.lparam, m.time,
                                static_cast<uint32_t>(m.x), static_cast<uint32_t>(m.y)};
    for (int i = 0; i < 7; ++i) cpu.write_u32(address + 4 * static_cast<uint32_t>(i), fields[i]);
}

// Cursor handles: the system's arrow, and kGameCursor + the Desktop's image number for the
// executable's own cursors.
constexpr uint32_t kArrowCursor = 0x00030001, kGameCursor = 0x00031000, kGameCursorEnd = 0x00032000;

// Data of the resource `type` / `id` (numbers; first language) of the executable loaded at `base`,
// read from its copy in memory, headers included: address and size, or nothing.
std::optional<std::pair<uint32_t, uint32_t>> find_resource(Cpu& cpu, uint32_t base, uint32_t type, uint32_t id) {
    const uint32_t directory = base + cpu.read_u32(base + 0x3C) + 24 + 96 + 2 * 8;  // IMAGE_DIRECTORY_ENTRY_RESOURCE
    const uint32_t rva = cpu.read_u32(directory);
    if (!rva || !cpu.read_u32(directory + 4)) return std::nullopt;
    const uint32_t root = base + rva;
    // A table: a 16-byte header (number of named entries at +12, of numbered ones at +14), then
    // 8 bytes per entry: name or number, then offset (high bit: a subtable).
    const auto entry = [&cpu, root](uint32_t table, std::optional<uint32_t> key) -> std::optional<uint32_t> {
        const uint32_t count = cpu.read_u16(table + 12) + cpu.read_u16(table + 14);
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t name = cpu.read_u32(table + 16 + 8 * i);
            if (!key || name == *key) return root + (cpu.read_u32(table + 20 + 8 * i) & 0x7FFFFFFF);
        }
        return std::nullopt;
    };
    const auto names = entry(root, type);
    const auto languages = names ? entry(*names, id) : std::nullopt;
    const auto data = languages ? entry(*languages, std::nullopt) : std::nullopt;
    if (!data) return std::nullopt;
    return std::make_pair(base + cpu.read_u32(*data), cpu.read_u32(*data + 4));
}

// Cursor `id` of the executable (GROUP_CURSOR, then the CURSOR of its first entry), decoded. A
// CURSOR is a hot spot (2 words) followed by a DIB of doubled height: colours (1, 4 or 8 bits,
// palette), then a 1-bit AND mask, rows from bottom to top. A mask bit of 1 is transparent (or
// inverts the screen when its colour is not zero: drawn black).
std::optional<CursorImage> load_cursor_resource(Cpu& cpu, uint32_t base, uint32_t id) {
    const auto group = find_resource(cpu, base, 12, id);  // RT_GROUP_CURSOR
    if (!group || group->second < 6 + 14 || cpu.read_u16(group->first + 4) == 0) return std::nullopt;
    const auto data = find_resource(cpu, base, 1, cpu.read_u16(group->first + 6 + 12));  // RT_CURSOR
    if (!data || data->second < 4 + 40) return std::nullopt;
    std::vector<uint8_t> bytes(data->second);
    cpu.read(data->first, bytes.data(), bytes.size());
    const auto u16 = [&bytes](size_t at) { return static_cast<uint32_t>(bytes[at] | bytes[at + 1] << 8); };
    const auto u32 = [&](size_t at) { return u16(at) | u16(at + 2) << 16; };
    CursorImage image;
    image.hot_x = static_cast<int>(u16(0));
    image.hot_y = static_cast<int>(u16(2));
    const size_t header = u32(4);
    image.width = static_cast<int>(u32(8));
    image.height = static_cast<int>(u32(12)) / 2;
    const uint32_t bits = u16(18), compression = u32(20), used = u32(36);
    if ((bits != 1 && bits != 4 && bits != 8) || compression != 0 || image.width <= 0 || image.width > 256 ||
        image.height <= 0 || image.height > 256)
        return std::nullopt;
    const size_t colors = used ? used : size_t{1} << bits;
    const size_t palette = 4 + header, pixels = palette + 4 * colors;
    const size_t stride = (static_cast<size_t>(image.width) * bits + 31) / 32 * 4, mask_stride = (static_cast<size_t>(image.width) + 31) / 32 * 4;
    const size_t mask = pixels + stride * image.height;
    if (mask + mask_stride * image.height > bytes.size()) return std::nullopt;
    image.argb.resize(static_cast<size_t>(image.width) * image.height);
    for (int y = 0; y < image.height; ++y) {
        const size_t row = static_cast<size_t>(image.height - 1 - y);
        for (int x = 0; x < image.width; ++x) {
            const size_t bit = static_cast<size_t>(x) * bits;
            const uint32_t index = (bytes[pixels + row * stride + bit / 8] >> (8 - bits - bit % 8)) & ((1u << bits) - 1);
            const bool transparent = (bytes[mask + row * mask_stride + x / 8] >> (7 - x % 8)) & 1;
            uint32_t& out = image.argb[static_cast<size_t>(y) * image.width + x];
            if (transparent) out = index ? 0xFF000000u : 0;
            else if (index < colors) out = 0xFF000000u | (u32(palette + 4 * index) & 0xFFFFFF);  // BGRx -> ARGB
            else out = 0xFF000000u;
        }
    }
    return image;
}

void write_rect(Cpu& cpu, uint32_t address, int left, int top, int right, int bottom) {
    const int32_t fields[4] = {left, top, right, bottom};
    for (int i = 0; i < 4; ++i) cpu.write_u32(address + 4 * static_cast<uint32_t>(i), static_cast<uint32_t>(fields[i]));
}

}  // namespace

void install_user32(Process& p) {
    auto st = std::make_shared<State>();
    Cpu& cpu = p.cpu();
    auto def_in = [&](const char* dll, const char* name, uint32_t args, std::function<uint32_t()> fn) {
        p.def(dll, name, args * 4, std::move(fn));
    };
    auto def = [&](const char* name, uint32_t args, std::function<uint32_t()> fn) {
        def_in("user32.dll", name, args, std::move(fn));
    };
    // Vide la file SDL ; arrête le programme si une limite de diagnostic est atteinte.
    auto pump = [&p] {
        if (!p.desktop().pump(p.ticks())) p.exit(0);
    };

    def("MessageBoxA", 4, [&p, &cpu] {
        std::fprintf(stderr, "[jeu] boîte de message « %s » : %s\n",
                     p.arg(2) ? to_utf8(cpu.read_string(p.arg(2))).c_str() : "",
                     p.arg(1) ? to_utf8(cpu.read_string(p.arg(1))).c_str() : "");
        return 1u;  // IDOK
    });

    // Première apparition de la fenêtre : Windows l'active, lui donne le clavier puis lui
    // annonce sa taille et sa position, par des messages envoyés avant de rendre la main.
    auto show = [&p, st](uint32_t result) {
        if (st->shown) return;
        st->shown = true;
        Desktop& d = p.desktop();
        const uint32_t size = static_cast<uint32_t>(d.height()) << 16 | static_cast<uint32_t>(d.width());
        d.queue.push_back({d.hwnd, WM_PAINT, 0, 0, p.ticks(), 0, 0});
        p.send_messages({{d.hwnd, WM_SHOWWINDOW, 1, 0},
                         {d.hwnd, WM_ACTIVATEAPP, 1, 0},
                         {d.hwnd, WM_ACTIVATE, 1, 0},
                         {d.hwnd, WM_SETFOCUS, 0, 0},
                         {d.hwnd, WM_SIZE, 0, size},
                         {d.hwnd, WM_MOVE, 0, 0}},
                        result);
    };

    // --- classes et fenêtre ------------------------------------------------------------
    def("RegisterClassA", 1, [&p, &cpu, st] {
        const uint32_t wc = p.arg(0);
        st->classes[cpu.read_string(cpu.read_u32(wc + 36))] = cpu.read_u32(wc + 4);
        return 0xC001u;
    });
    def("FindWindowA", 2, [] { return 0u; });  // aucune autre instance du jeu
    def("CreateWindowExA", 12, [&p, &cpu, st, show] {
        const std::string class_name = cpu.read_string(p.arg(1));
        const std::string title = p.arg(2) ? to_utf8(cpu.read_string(p.arg(2))) : "Zeus";
        const int32_t w = static_cast<int32_t>(p.arg(6)), h = static_cast<int32_t>(p.arg(7));
        Desktop& desktop = p.desktop();
        if (p.trace)
            std::fprintf(stderr, "    fenêtre « %s » : style %#x, style étendu %#x, position (%d,%d), taille %dx%d\n",
                         title.c_str(), p.arg(3), p.arg(0), static_cast<int32_t>(p.arg(4)),
                         static_cast<int32_t>(p.arg(5)), w, h);
        const auto found = st->classes.find(class_name);
        if (found == st->classes.end()) return 0u;
        desktop.wndproc = found->second;
        const bool sized = w > 0 && h > 0 && w <= 4096 && h <= 4096;
        desktop.create_window(title, sized ? w : 0, sized ? h : 0);
        // CREATESTRUCT transmis avec WM_CREATE.
        const uint32_t cs = p.alloc(48);
        cpu.write_u32(cs + 0, p.arg(11));
        cpu.write_u32(cs + 4, p.arg(10));
        cpu.write_u32(cs + 16, static_cast<uint32_t>(desktop.height()));
        cpu.write_u32(cs + 20, static_cast<uint32_t>(desktop.width()));
        cpu.write_u32(cs + 32, p.arg(3));
        cpu.write_u32(cs + 36, p.arg(2));
        cpu.write_u32(cs + 40, p.arg(1));
        cpu.write_u32(cs + 44, p.arg(0));
        const uint32_t hwnd = desktop.hwnd;
        st->window_longs[GWL_STYLE] = p.arg(3);
        st->window_longs[GWL_EXSTYLE] = p.arg(0);
        const bool visible = p.arg(3) & WS_VISIBLE;
        cpu.call_guest(desktop.wndproc, {hwnd, WM_CREATE, 0, cs}, [&p, st, show, cs, hwnd, visible](uint32_t result) {
            p.release(cs);
            if (result == 0xFFFFFFFF) return 0u;
            // Une fenêtre créée visible est montrée avant le retour de CreateWindowEx.
            if (visible) show(hwnd);
            return hwnd;
        });
        return 0u;
    });
    def("ShowWindow", 2, [&p, st, show] {
        const bool was_visible = st->shown;
        if (p.arg(1) != 0) show(was_visible ? 1u : 0u);
        return was_visible ? 1u : 0u;
    });
    def("UpdateWindow", 1, [] { return 1u; });
    def("SetFocus", 1, [&p] { return p.desktop().hwnd; });
    def("SetForegroundWindow", 1, [] { return 1u; });
    def("GetForegroundWindow", 0, [&p] { return p.desktop().hwnd; });
    def("GetDesktopWindow", 0, [] { return 0x00010000u; });
    def("IsIconic", 1, [] { return 0u; });
    def("OpenIcon", 1, [] { return 1u; });
    def("GetMenu", 1, [] { return 0u; });
    def("SetWindowPos", 7, [] { return 1u; });
    def("InvalidateRect", 3, [] { return 1u; });
    def("GetWindowLongA", 2, [&p, st] {
        const auto found = st->window_longs.find(static_cast<int32_t>(p.arg(1)));
        return found == st->window_longs.end() ? 0u : found->second;
    });
    def("SetWindowLongA", 3, [&p, st] {
        const int32_t index = static_cast<int32_t>(p.arg(1));
        if (p.trace) std::fprintf(stderr, "    SetWindowLong(%d) = %#x\n", index, p.arg(2));
        const uint32_t previous = st->window_longs[index];
        st->window_longs[index] = p.arg(2);
        return previous;
    });
    def("GetClientRect", 2, [&p, &cpu] {
        write_rect(cpu, p.arg(1), 0, 0, p.desktop().width(), p.desktop().height());
        return 1u;
    });
    def("GetWindowRect", 2, [&p, &cpu] {
        write_rect(cpu, p.arg(1), 0, 0, p.desktop().width(), p.desktop().height());
        return 1u;
    });
    def("AdjustWindowRectEx", 4, [] { return 1u; });  // fenêtre sans bordure : rectangle inchangé
    def("ClientToScreen", 2, [] { return 1u; });      // la fenêtre est à l'origine de l'écran
    def("SetRect", 5, [&p, &cpu] {
        write_rect(cpu, p.arg(0), static_cast<int32_t>(p.arg(1)), static_cast<int32_t>(p.arg(2)),
                   static_cast<int32_t>(p.arg(3)), static_cast<int32_t>(p.arg(4)));
        return 1u;
    });
    def("PtInRect", 3, [&p, &cpu] {
        const int32_t x = static_cast<int32_t>(p.arg(1)), y = static_cast<int32_t>(p.arg(2));
        const uint32_t r = p.arg(0);
        return x >= static_cast<int32_t>(cpu.read_u32(r)) && x < static_cast<int32_t>(cpu.read_u32(r + 8)) &&
                       y >= static_cast<int32_t>(cpu.read_u32(r + 4)) && y < static_cast<int32_t>(cpu.read_u32(r + 12))
                   ? 1u
                   : 0u;
    });
    def("GetDC", 1, [] { return 0x00020000u; });
    def("ReleaseDC", 2, [] { return 1u; });
    def("BeginPaint", 2, [&p, &cpu] {
        cpu.fill(p.arg(1), 0, 64);
        cpu.write_u32(p.arg(1), 0x00020000);
        write_rect(cpu, p.arg(1) + 8, 0, 0, p.desktop().width(), p.desktop().height());
        return 0x00020000u;
    });
    def("EndPaint", 2, [] { return 1u; });

    // --- système, curseur, clavier -----------------------------------------------------
    def("GetSystemMetrics", 1, [&p] {
        switch (p.arg(0)) {
            case 0: return static_cast<uint32_t>(p.desktop().width());   // SM_CXSCREEN
            case 1: return static_cast<uint32_t>(p.desktop().height());  // SM_CYSCREEN
            case 19: return 1u;    // SM_MOUSEPRESENT
            default: return 0u;
        }
    });
    def("SystemParametersInfoA", 4, [] { return 1u; });
    // Cursors: the executable's own (the land-clearing shovel, the sword, the magnifier, the arrow)
    // are shown at the scale of the picture; the system's cursors (IDC_…) give the arrow.
    def("LoadCursorA", 2, [&p, &cpu, st] {
        const uint32_t module = p.arg(0), name = p.arg(1);
        if (module == 0 || name >= 0x10000) return kArrowCursor;
        if (const auto known = st->cursors.find(name); known != st->cursors.end()) return known->second;
        uint32_t handle = kArrowCursor;
        if (auto image = load_cursor_resource(cpu, p.image().image_base, name)) {
            const int index = p.desktop().add_cursor(std::move(*image));
            handle = kGameCursor + static_cast<uint32_t>(index);
            // The game's window class has no cursor: until its first SetCursor, Windows showed its
            // own arrow, which the game's arrow (the first cursor it loads, resource 102) imitates.
            // Rather than macOS's arrow, of another design, the host shows the game's from the start.
            if (!st->cursor_set && index == 0) p.desktop().set_cursor(index);
        }
        st->cursors[name] = handle;
        if (st->log_cursors) std::fprintf(stderr, "[hôte] cursor %u -> %#x\n", name, handle);
        return handle;
    });
    def("LoadIconA", 2, [] { return 0x00030002u; });
    def("SetCursor", 1, [&p, st] {
        const uint32_t previous = st->cursor, handle = p.arg(0);
        if (st->log_cursors && handle != previous) std::fprintf(stderr, "[hôte] SetCursor %#x\n", handle);
        st->cursor = handle;
        st->cursor_set = true;
        p.desktop().set_cursor(handle >= kGameCursor && handle < kGameCursorEnd ? static_cast<int>(handle - kGameCursor)
                                                                                : Desktop::kSystemCursor);
        return previous;
    });
    def("ShowCursor", 1, [&p, st] {
        st->cursor_count += p.arg(0) ? 1 : -1;
        return static_cast<uint32_t>(st->cursor_count);
    });
    def("GetCursorPos", 1, [&p, &cpu] {
        cpu.write_u32(p.arg(0), static_cast<uint32_t>(p.desktop().mouse_x));
        cpu.write_u32(p.arg(0) + 4, static_cast<uint32_t>(p.desktop().mouse_y));
        return 1u;
    });
    def("GetAsyncKeyState", 1, [&p] {
        return p.desktop().key_down(static_cast<int>(p.arg(0))) ? 0x8000u : 0u;
    });
    def("RegisterWindowMessageA", 1, [st] { return st->next_message_id++; });

    // --- messages ----------------------------------------------------------------------
    def("PeekMessageA", 5, [&p, &cpu, st, pump] {
        pump();
        Desktop& d = p.desktop();
        const uint32_t low = p.arg(2), high = p.arg(3);
        if (p.trace)
            std::fprintf(stderr, "    PeekMessage fenêtre %#x, messages %#x à %#x, mode %#x, %zu en attente\n", p.arg(1),
                         low, high, p.arg(4), d.queue.size());
        for (auto it = d.queue.begin(); it != d.queue.end(); ++it) {
            if ((low || high) && (it->message < low || it->message > high)) continue;
            write_message(cpu, p.arg(0), *it);
            if (p.arg(4) & 1) d.queue.erase(it);  // PM_REMOVE
            return 1u;
        }
        // La boucle du jeu interroge sa file sans jamais attendre et redessine à chaque tour.
        // Rien à lire : on limite la boucle à 60 tours par seconde, ce qui libère le processeur
        // sans retard sensible des entrées. Mais le jeu ne fait un tour de simulation que dans un
        // tour de boucle où son intervalle est écoulé : attendre toujours 16 ms arrondirait cet
        // intervalle au multiple de 16 ms suivant (22 ms à 90 % deviendraient 33 ms, soit le jeu
        // aux deux tiers de sa vitesse). Quand le prochain tour de simulation tombe dans les deux
        // créneaux à venir, on attend donc exactement jusqu'à lui.
        constexpr uint32_t kTurn = 16;  // millisecondes
        if (p.fixed_clock) return 0u;
        const uint32_t now = p.ticks();
        uint32_t wait = now - st->last_idle_poll < kTurn ? kTurn - (now - st->last_idle_poll) : 0;
        std::optional<uint32_t> exact;  // instant à atteindre au plus près
        if (!st->knows_turns) {
            st->knows_turns = gog::matches(cpu, gog::kSimulationDue, {0x56, 0x57, 0xFF, 0x15, 0x24, 0x82, 0x5D, 0x00,
                                                                      0x8B, 0xF8, 0xA1, 0x98, 0xF9, 0xF9, 0x00});
        }
        if (*st->knows_turns) {
            if (const std::optional<uint32_t> due = next_simulation_turn(cpu)) {
                const int32_t until = static_cast<int32_t>(*due - now);
                if (until > 0 && until <= static_cast<int32_t>(2 * kTurn)) {
                    wait = static_cast<uint32_t>(until);
                    exact = due;
                }
            }
        }
        if (exact) {
            // L'attente de SDL dépasse souvent d'une milliseconde : on dort un peu moins, puis on
            // cède la main jusqu'à l'instant voulu.
            if (wait > 1) d.wait(static_cast<int>(wait - 1));
            while (static_cast<int32_t>(*exact - p.ticks()) > 0) std::this_thread::yield();
        } else if (wait) {
            d.wait(static_cast<int>(wait));
        }
        st->last_idle_poll = p.ticks();
        return 0u;
    });
    def("GetMessageA", 4, [&p, &cpu, pump] {
        Desktop& d = p.desktop();
        for (;;) {
            pump();
            if (p.cpu().stopped()) return 0u;
            if (!d.queue.empty()) break;
            d.wait(5);
        }
        const Message m = d.queue.front();
        d.queue.pop_front();
        write_message(cpu, p.arg(0), m);
        return m.message == WM_QUIT ? 0u : 1u;
    });
    def("WaitMessage", 0, [&p, pump] {
        Desktop& d = p.desktop();
        for (;;) {
            pump();
            if (p.cpu().stopped() || !d.queue.empty()) return 1u;
            d.wait(5);
        }
    });
    def("TranslateMessage", 1, [] { return 1u; });  // les WM_CHAR viennent déjà de la saisie SDL
    def("PostMessageA", 4, [&p] {
        p.desktop().queue.push_back({p.arg(0), p.arg(1), p.arg(2), p.arg(3), p.ticks(), 0, 0});
        return 1u;
    });
    def("PostQuitMessage", 1, [&p] {
        p.desktop().queue.push_back({0, WM_QUIT, p.arg(0), 0, p.ticks(), 0, 0});
        return 0u;
    });
    def("DispatchMessageA", 1, [&p, &cpu] {
        const uint32_t m = p.arg(0);
        const uint32_t wndproc = p.desktop().wndproc;
        if (!wndproc) return 0u;
        cpu.call_guest(wndproc, {cpu.read_u32(m), cpu.read_u32(m + 4), cpu.read_u32(m + 8), cpu.read_u32(m + 12)},
                       [](uint32_t result) { return result; });
        return 0u;
    });
    def("SendMessageA", 4, [&p, &cpu] {
        const uint32_t wndproc = p.desktop().wndproc;
        if (!wndproc) return 0u;
        cpu.call_guest(wndproc, {p.arg(0), p.arg(1), p.arg(2), p.arg(3)}, [](uint32_t result) { return result; });
        return 0u;
    });
    def("DefWindowProcA", 4, [&p, &cpu] {
        if (p.arg(1) == WM_CLOSE && p.desktop().wndproc) {
            // Par défaut, fermer la fenêtre la détruit : le programme reçoit WM_DESTROY.
            cpu.call_guest(p.desktop().wndproc, {p.arg(0), WM_DESTROY, 0, 0}, [](uint32_t) { return 0u; });
        }
        return 0u;
    });

    // --- GDI32, WINMM, OLE32, SHELL32 --------------------------------------------------
    def_in("gdi32.dll", "GetDeviceCaps", 2, [&p] {
        switch (p.arg(1)) {
            case 8: return static_cast<uint32_t>(p.desktop().width());    // HORZRES
            case 10: return static_cast<uint32_t>(p.desktop().height());  // VERTRES
            case 12: return 16u;                                          // BITSPIXEL
            case 14: return 1u;                                           // PLANES
            default: return 0u;
        }
    });
    def_in("gdi32.dll", "GetStockObject", 1, [] { return 0x00040000u; });
    def_in("gdi32.dll", "GetPaletteEntries", 4, [] { return 0u; });
    def_in("winmm.dll", "timeGetTime", 0, [&p] { return p.ticks(); });
    def_in("ole32.dll", "CoInitialize", 1, [] { return 0u; });
    def_in("ole32.dll", "CoUninitialize", 0, [] { return 0u; });
    def_in("shell32.dll", "ShellExecuteA", 6, [&p, &cpu] {
        std::fprintf(stderr, "[hôte] ShellExecuteA(%s) ignoré\n", p.arg(2) ? cpu.read_string(p.arg(2)).c_str() : "");
        return 33u;  // toute valeur supérieure à 32 signifie « réussi »
    });
}

}  // namespace zeus::win32
