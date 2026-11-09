// Frame pacing: every simulation tick gets a presented frame (host).
//
// The game draws the city in each main-loop turn after the simulation ticks that are due
// (game_turn 0x00513580), but FUN_0050c1b0 presents a frame only when its elapsed-time
// accumulator (0x00F9C0F0) reaches 50 ms, or when 0x00F2A6CC forces it (rendering.md §3.3).
// The walkers move once per tick, and a tick is due every interval of the game speed plus 2 ms
// (22 ms at the default 90 %, back to back at 100 %). Since the host runs a turn only when there is
// input, when a tick is due or every 16 ms (PeekMessageA), the accumulator crosses 50 ms on the
// third tick at 90 %: 15 frames per second, each showing three ticks of movement. At 100 % each
// frame showed three or four ticks, sometimes a dozen. The walkers, the animals and the building
// animations moved in jerks, the more so as the game is faster.
//
// At the presentation's decision (0x0050C1EC, where it reads 0x00F2A6CC), the host forces the
// frame when a tick has happened since the last presented frame and at least kMinFrameMs have
// passed: every tick is shown, up to about 60 frames per second. The frames are the game's own;
// only more of them reach the screen. Each presentation waits for the display's refresh, so a
// faster game pays for it: in a window, in the large city of the last tutorial, 44.4 → 42.5 ticks
// per second at 90 % (xBRZ on) and 66.5 → 59.2 at 100 % (plain pixels).

#include <cstdio>
#include <cstdlib>
#include <memory>

#include "win32/game_gog.h"
#include "win32/process.h"

namespace zeus::win32 {
namespace {

using namespace gog;

constexpr uint32_t kPresentDecision = 0x0050C1EC;  // MOV CL,[0x00F2A6CC]
constexpr uint32_t kPresenting = 0x0050C204;       // the frame will be presented
constexpr uint32_t kForcePresent = 0x00F2A6CC;
constexpr uint32_t kMinFrameMs = 15;  // about 60 frames per second at most

struct State {
    int ticks = 0;  // simulation ticks since the last presented frame
    uint32_t last_present = 0;
    // Diagnostics: ZEUS_PACE_LOG=1 prints, for each presented frame, its time, the time since the
    // previous one, the ticks it shows and how long the previous present() took;
    // ZEUS_FORCE_SPEED=N sets the game speed to N % at every tick; ZEUS_PACE_OBSERVE=1 logs
    // without forcing anything (the game's own pacing, for comparison).
    bool log = std::getenv("ZEUS_PACE_LOG") != nullptr;
    int force_speed = std::getenv("ZEUS_FORCE_SPEED") ? std::atoi(std::getenv("ZEUS_FORCE_SPEED")) : 0;
    bool observe = std::getenv("ZEUS_PACE_OBSERVE") != nullptr;
};

}  // namespace

void install_frame_pacing(Process& p) {
    Cpu& cpu = p.cpu();
    if (!matches(cpu, kSimulationTick, {0xA1, 0x24, 0x36, 0xF1, 0x00}) ||
        !matches(cpu, kPresentDecision, {0x8A, 0x0D, 0xCC, 0xA6, 0xF2, 0x00}) ||
        !matches(cpu, kPresenting, {0x8D, 0x44, 0x24, 0x18})) {
        std::fprintf(stderr, "[hôte] frame pacing: unknown executable, not installed\n");
        return;
    }
    auto st = std::make_shared<State>();
    Process* process = &p;
    cpu.add_code_hook(kSimulationTick, [st](Cpu& c) {
        ++st->ticks;
        if (st->force_speed > 0) c.write_u32(kGameSpeed, static_cast<uint32_t>(st->force_speed));
    });
    cpu.add_code_hook(kPresentDecision, [st, process](Cpu& c) {
        if (st->ticks == 0 || st->observe) return;
        if (process->peek_ticks() - st->last_present >= kMinFrameMs) c.write_u8(kForcePresent, 1);
    });
    cpu.add_code_hook(kPresenting, [st, process](Cpu&) {
        const uint32_t now = process->peek_ticks();
        if (st->log)
            std::fprintf(stderr, "[pace] %u %u %d %.1f\n", now, now - st->last_present, st->ticks, process->desktop().last_present_ms());
        st->ticks = 0;
        st->last_present = now;
    });
}

}  // namespace zeus::win32
