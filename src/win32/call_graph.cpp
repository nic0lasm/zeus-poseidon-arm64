#include "win32/call_graph.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "win32/cpu.h"
#include "win32/game_gog.h"
#include "win32/process.h"

namespace zeus::win32 {

namespace {

uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

}  // namespace

// Les appels sont reconnus sans désassembler : un « call » termine son bloc et empile l'adresse
// qui le suit, si bien qu'à l'entrée de la fonction appelée le sommet de la pile vaut la fin du
// bloc précédent. Une fonction est quittée dès que ESP repasse au-dessus de sa valeur d'entrée
// (« ret », mais aussi longjmp et déroulement d'exception). Les sauts d'une fonction à une autre
// (appels terminaux) ne sont pas des appels : leurs blocs restent comptés à la fonction qui a
// sauté, mais le bloc lui-même garde son adresse, que la carte du code rattache à sa fonction.
class CallGraph {
public:
    std::string path;
    struct Frame {
        uint32_t entry = 0, esp = 0;
        uint64_t since = 0, blocks_since = 0;
    };
    std::vector<Frame> stack;
    std::unordered_map<uint32_t, int> active;  // activations en cours de chaque fonction
    int ticks_open = 0;                        // tours de simulation sur la pile
    uint32_t block = 0, block_size = 0, block_end = 0;
    bool block_in_tick = false;
    uint64_t block_since = 0;
    struct BlockCost {
        uint32_t bytes = 0;
        uint64_t executions[2] = {}, nanoseconds[2] = {};  // [0] hors du tour, [1] pendant
    };
    std::unordered_map<uint32_t, BlockCost> blocks;
    struct Calls {
        uint64_t count[2] = {};
    };
    std::unordered_map<uint64_t, Calls> calls;  // (site d'appel << 32) | fonction appelée
    struct Function {
        uint64_t calls = 0, inclusive = 0, inclusive_blocks = 0;
    };
    std::unordered_map<uint32_t, Function> functions;
    uint64_t ticks = 0, blocks_run = 0;

    void pop(uint64_t now) {
        const Frame frame = stack.back();
        stack.pop_back();
        if (--active[frame.entry] == 0) {
            Function& function = functions[frame.entry];
            function.inclusive += now - frame.since;
            function.inclusive_blocks += blocks_run - frame.blocks_since;
        }
        if (frame.entry == gog::kSimulationTick) --ticks_open;
    }

    void on_block(Cpu& cpu, uint32_t address, uint32_t size) {
        const uint64_t now = now_ns();
        if (block) {
            BlockCost& cost = blocks[block];
            cost.bytes = block_size;
            ++cost.executions[block_in_tick];
            cost.nanoseconds[block_in_tick] += now - block_since;
        }
        const uint32_t esp = cpu.reg(Cpu::Reg::ESP);
        while (!stack.empty() && esp > stack.back().esp) pop(now);
        if (address != block_end && block_end && cpu.read_u32(esp) == block_end) {
            const bool in_tick = ticks_open > 0;
            ++calls[(static_cast<uint64_t>(block) << 32) | address].count[in_tick];
            ++functions[address].calls;
            ++active[address];
            stack.push_back({address, esp, now, blocks_run});
            if (address == gog::kSimulationTick) {
                ++ticks_open;
                ++ticks;
            }
        }
        ++blocks_run;
        block = address;
        block_size = size;
        block_end = address + size;
        block_in_tick = ticks_open > 0;
        block_since = now;
    }
};

std::shared_ptr<CallGraph> install_call_graph(Process& process, const std::string& path) {
    auto graph = std::make_shared<CallGraph>();
    graph->path = path;
    process.cpu().add_block_hook([graph](Cpu& cpu, uint32_t address, uint32_t size) { graph->on_block(cpu, address, size); });
    return graph;
}

void finish_call_graph(CallGraph& graph) {
    const uint64_t now = now_ns();
    while (!graph.stack.empty()) graph.pop(now);
    std::ofstream out(graph.path);
    if (!out) throw std::runtime_error("arbre d'appels : impossible d'écrire " + graph.path);
    out << "# zeus-run --call-graph : " << graph.ticks << " tours de simulation (FUN_00513a30)\n"
        << "# bloc ADRESSE OCTETS EXÉCUTIONS_HORS_TOUR NS_HORS_TOUR EXÉCUTIONS_TOUR NS_TOUR\n"
        << "# appel SITE APPELÉE APPELS_HORS_TOUR APPELS_TOUR (SITE : bloc qui se termine par l'appel)\n"
        << "# fonction ADRESSE APPELS NS_INCLUS BLOCS_INCLUS (appel le plus extérieur, appelées comprises)\n";
    out << std::hex;
    for (const auto& [address, cost] : graph.blocks)
        out << "bloc\t" << address << std::dec << '\t' << cost.bytes << '\t' << cost.executions[0] << '\t'
            << cost.nanoseconds[0] << '\t' << cost.executions[1] << '\t' << cost.nanoseconds[1] << std::hex << '\n';
    for (const auto& [key, calls] : graph.calls)
        out << "appel\t" << (key >> 32) << '\t' << (key & 0xFFFFFFFFu) << std::dec << '\t' << calls.count[0] << '\t'
            << calls.count[1] << std::hex << '\n';
    for (const auto& [address, function] : graph.functions)
        out << "fonction\t" << address << std::dec << '\t' << function.calls << '\t' << function.inclusive << '\t' << function.inclusive_blocks << std::hex << '\n';
}

class TimeSpans {
public:
    struct Span {
        uint32_t start = 0, end = 0;
        uint64_t calls = 0, nanoseconds = 0, since = 0;
        bool open = false;
    };
    std::vector<Span> spans;
    uint64_t began = now_ns();
};

std::shared_ptr<TimeSpans> install_time_spans(Process& process, const std::string& spec) {
    auto spans = std::make_shared<TimeSpans>();
    for (size_t at = 0; at < spec.size();) {
        const size_t comma = std::min(spec.find(',', at), spec.size());
        const std::string item = spec.substr(at, comma - at);
        const size_t colon = item.find(':');
        if (colon == std::string::npos) throw std::runtime_error("--time-span attend DÉBUT:RETOUR,…");
        TimeSpans::Span span;
        span.start = static_cast<uint32_t>(std::stoul(item.substr(0, colon), nullptr, 16));
        span.end = static_cast<uint32_t>(std::stoul(item.substr(colon + 1), nullptr, 16));
        spans->spans.push_back(span);
        at = comma + 1;
    }
    for (size_t i = 0; i < spans->spans.size(); ++i) {
        const TimeSpans::Span& span = spans->spans[i];
        process.cpu().add_code_hook(span.start, [spans, i](Cpu&) {
            TimeSpans::Span& s = spans->spans[i];
            if (!s.open) {
                s.open = true;
                s.since = now_ns();
            }
        });
        process.cpu().add_code_hook(span.end, [spans, i](Cpu&) {
            TimeSpans::Span& s = spans->spans[i];
            if (s.open) {
                s.open = false;
                s.nanoseconds += now_ns() - s.since;
                ++s.calls;
            }
        });
    }
    spans->began = now_ns();
    return spans;
}

void report_time_spans(const TimeSpans& spans) {
    const double wall = static_cast<double>(now_ns() - spans.began);
    for (const TimeSpans::Span& s : spans.spans)
        std::fprintf(stderr, "durée %#010x → %#010x : %llu passages, %.1f ms, %.3f ms par passage, %.1f %% du temps écoulé\n",
                     s.start, s.end, static_cast<unsigned long long>(s.calls), s.nanoseconds / 1e6,
                     s.calls ? s.nanoseconds / 1e6 / s.calls : 0.0, 100.0 * s.nanoseconds / wall);
}

}  // namespace zeus::win32
