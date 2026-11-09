// Smooth scrolling of the city map (host).
//
// The game's camera is a tile (the column and row given to FUN_0044a830, which stores them at
// 0x007D3BD0 / 0x007D3BCC): each scrolling step moves the picture by a whole tile at once, 60
// pixels in x or 4 rows of 15 pixels in y (measured: arrow keys, one tile per key press). Enlarged
// to a Mac's screen, the picture jumps.
//
// The host shifts the origin of the view (x0, y0 at 0x007D3BE8 / 0x007D3BE4, rendering.md §10.2)
// while the city is drawn, and only then, from the entry of FUN_0044a830 to its RET (0x0044A927):
// right after a step, the picture is drawn where it was, then the shift goes back to zero in a
// fraction of a second. Every pass of the drawing (ground, buildings, walkers, construction preview,
// numbers) computes its positions from x0 and y0; the host's native functions receive positions
// already computed. The view's clipping rectangle is copied from other variables (0x007D3B88 to
// 0x007D3B94) and does not move; the game draws tiles beyond the edges of the view anyway (6 columns
// on the left, 2 on the right, 8 rows above, rendering.md §10.3), which fill the shift, bounded
// accordingly. Outside the drawing, x0 and y0 keep their value: clicks aim at the real tiles.
// Without scrolling, the shift is zero and nothing is touched: the frames are the game's own.
//
// A jump of more than 3 tiles (a click on the mini-map, going to an event) or a change of the
// view's orientation is not animated.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "win32/game_gog.h"
#include "win32/native.h"
#include "win32/process.h"

namespace zeus::win32 {
namespace {

using namespace gog;

constexpr uint32_t kCityViewDraw = 0x0044A830;     // FUN_0044a830(column, row, …)
constexpr uint32_t kCityViewDrawRet = 0x0044A927;  // its only RET
constexpr uint32_t kViewX0 = 0x007D3BE8, kViewY0 = 0x007D3BE4;
// FUN_0050c1b0 presents the frame only every 50 ms (20 frames per second), unless this byte is not
// zero (rendering.md §3.3): during the animation, every frame drawn is presented.
constexpr uint32_t kForcePresent = 0x00F2A6CC;
constexpr double kSingleStepMs = 150;  // a lone step is caught up in about 0.17 s
constexpr double kCatchUpMs = 150;     // lag beyond one step: caught up on top, proportionally
// The shift goes back a little slower than the steps come: the reserve this leaves (about twenty
// pixels) absorbs a step that arrives one frame later than the others, which the game does while
// a key is held (a step every 51 ms, sometimes 68), without the picture stopping.
constexpr double kSpeedShare = 0.85;
// Bounds of the shift: the game draws 6 more columns left of the view, 2 on its right and 8 rows
// above it (rendering.md §10.3); 110 pixels stay covered in all four directions (checked with
// ZEUS_SMOOTH_FIXED, diamonds clipped to the pixel).
constexpr double kMaxX = 110, kMaxY = 110;

struct State {
    bool known = false, applied = false, moving = false;
    int32_t column = 0, row = 0;
    uint32_t orientation = 0, last_ms = 0, last_step_ms = 0;
    double speed = 0;               // pixels per millisecond: the pace of the game's steps, smoothed
    double step = 60;               // length of the last step, in pixels
    double dx = 0, dy = 0;          // shift of the origin, in game pixels
    int32_t saved_x = 0, saved_y = 0;
    // Diagnostics: ZEUS_SMOOTH_LOG=1 traces the camera's steps, =2 every frame;
    // ZEUS_SMOOTH_FIXED=dx,dy forces a fixed shift (to check how the diamonds are clipped).
    const char* log_env = std::getenv("ZEUS_SMOOTH_LOG");
    bool log = log_env != nullptr, log_frames = log_env && log_env[0] == '2';
    bool fixed = false;
    int32_t fixed_x = 0, fixed_y = 0;
};

bool city_screen(Cpu& c) {
    const uint8_t s = c.read_u8(kScreen);
    return s == 0x01 || s == 0x20 || s == 0x48 || s == 0x56 || s == 0x58;
}

}  // namespace

void install_smooth_scroll(Process& p) {
    Cpu& cpu = p.cpu();
    // The bytes of the known executable: "MOV EAX,[0x847D14]" at the entry, "RET" at the end.
    if (!matches(cpu, kCityViewDraw, {0xA1, 0x14, 0x7D, 0x84, 0x00}) || !matches(cpu, kCityViewDrawRet, {0xC3})) {
        std::fprintf(stderr, "[hôte] smooth scrolling: unknown executable, not installed\n");
        return;
    }
    auto st = std::make_shared<State>();
    if (const char* fixed = std::getenv("ZEUS_SMOOTH_FIXED"))
        st->fixed = std::sscanf(fixed, "%d,%d", &st->fixed_x, &st->fixed_y) == 2;
    Process* process = &p;
    cpu.add_code_hook(kCityViewDraw, [st, process](Cpu& c) {
        const int32_t column = static_cast<int32_t>(c.arg(0)), row = static_cast<int32_t>(c.arg(1));
        const uint32_t orientation = c.read_u32(kViewOrientation);
        const uint32_t now = process->peek_ticks();  // without advancing the fixed clock of test runs
        if (st->log_frames)
            std::fprintf(stderr, "[hôte] draw %u ms camera %d,%d shift %.0f,%.0f frames %d\n", now, column, row, st->dx, st->dy,
                         process->desktop().presents());
        if (!city_screen(c)) {
            st->known = false;
            st->dx = st->dy = 0;
            return;
        }
        // The shift goes back towards zero at the pace of the game's steps: continuous scrolling
        // (a held key, the mouse at the edge) becomes even. Right after a step, the lag is one
        // step; when it is more (steps closer together than expected), the excess is caught up on
        // top, little by little, instead of being dropped at once.
        if (st->known) {
            const double dt = static_cast<double>(now - st->last_ms);
            const double length = std::hypot(st->dx, st->dy);
            if (length > 0) {
                const double move = (kSpeedShare * st->speed + std::fmax(0.0, length - st->step) / kCatchUpMs) * dt;
                const double k = move >= length ? 0 : (length - move) / length;
                st->dx *= k;
                st->dy *= k;
            }
        }
        if (st->known && (column != st->column || row != st->row)) {
            const int32_t dc = column - st->column, dr = row - st->row;
            if (orientation != st->orientation || std::abs(dc) > 3 || std::abs(dr) > 12 || (dr & 1)) {
                st->dx = st->dy = 0;  // a jump, or a step of an odd number of rows (their offset was not measured)
            } else {
                // The camera moves by (60 dc, 15 dr) pixels: the picture moves back as much, which
                // the shifted origin first makes up for.
                st->dx += 60.0 * dc;
                st->dy += 15.0 * dr;
                // Pace of the steps: a moving average, since key repeats do not arrive at regular
                // intervals from one frame to the next; a lone step (the first of a series) is
                // caught up in kSingleStepMs.
                const double step = std::hypot(60.0 * dc, 15.0 * dr);
                const double interval = static_cast<double>(now - st->last_step_ms);
                if (interval > 0 && interval < 2 * kSingleStepMs)
                    st->speed = 0.5 * st->speed + 0.5 * step / interval;
                else
                    st->speed = step / kSingleStepMs;
                st->step = step;
                st->last_step_ms = now;
            }
            if (st->log) std::fprintf(stderr, "[hôte] camera %d,%d → %d,%d\n", st->column, st->row, column, row);
        }
        st->dx = std::fmax(-kMaxX, std::fmin(kMaxX, st->dx));
        st->dy = std::fmax(-kMaxY, std::fmin(kMaxY, st->dy));
        if (std::fabs(st->dx) < 0.5) st->dx = 0;
        if (std::fabs(st->dy) < 0.5) st->dy = 0;
        st->known = true;
        st->column = column;
        st->row = row;
        st->orientation = orientation;
        st->last_ms = now;
        int32_t ox = static_cast<int32_t>(std::lround(st->dx)), oy = static_cast<int32_t>(std::lround(st->dy));
        if (st->fixed) ox = st->fixed_x, oy = st->fixed_y;
        // During the animation, and for the frame where it ends, every frame is presented.
        if (ox != 0 || oy != 0 || st->moving) c.write_u8(kForcePresent, 1);
        st->moving = ox != 0 || oy != 0;
        if (ox == 0 && oy == 0) return;
        st->saved_x = static_cast<int32_t>(c.read_u32(kViewX0));
        st->saved_y = static_cast<int32_t>(c.read_u32(kViewY0));
        c.write_u32(kViewX0, static_cast<uint32_t>(st->saved_x + ox));
        c.write_u32(kViewY0, static_cast<uint32_t>(st->saved_y + oy));
        set_precise_ground(true);  // the game can only clip the diamonds by halves
        st->applied = true;
    });
    cpu.add_code_hook(kCityViewDrawRet, [st](Cpu& c) {
        if (!st->applied) return;
        c.write_u32(kViewX0, static_cast<uint32_t>(st->saved_x));
        c.write_u32(kViewY0, static_cast<uint32_t>(st->saved_y));
        set_precise_ground(false);
        st->applied = false;
    });
}

}  // namespace zeus::win32
