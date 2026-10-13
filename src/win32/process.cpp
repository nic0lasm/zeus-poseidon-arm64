#include "win32/process.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace zeus::win32 {
namespace {

// Disposition de la mémoire émulée.
constexpr uint32_t kHeapBase = 0x10000000, kHeapSize = 0x10000000;   // 256 Mo
constexpr uint32_t kStackBase = 0x0FE00000, kStackSize = 0x00200000; // 2 Mo
constexpr uint32_t kTeb = 0x7FFDE000, kPeb = 0x7FFDF000;
constexpr uint32_t kExitAddress = 0xF000FFF0;  // adresse de retour du point d'entrée

}  // namespace

Process::Process(std::filesystem::path game_dir, std::filesystem::path write_dir)
    : game_dir_(std::move(game_dir)), write_dir_(std::move(write_dir)),
      vfs_(game_dir_, write_dir_, kGuestDir), heap_next_(kHeapBase) {
    cpu_.map(kHeapBase, kHeapSize);
    cpu_.map(kStackBase, kStackSize);
    cpu_.map(kTeb, 0x2000);
    // Bloc d'environnement du fil : chaîne des gestionnaires d'exception, bornes de la pile.
    cpu_.write_u32(kTeb + 0x00, 0xFFFFFFFF);
    cpu_.write_u32(kTeb + 0x04, kStackBase + kStackSize);
    cpu_.write_u32(kTeb + 0x08, kStackBase);
    cpu_.write_u32(kTeb + 0x18, kTeb);
    cpu_.write_u32(kTeb + 0x30, kPeb);
    cpu_.set_fs_base(kTeb);
}

void Process::load(const std::filesystem::path& exe) {
    image_ = load_pe(exe, cpu_);
    cpu_.write_u32(kPeb + 0x08, image_.image_base);
    bind_imports();
}

void Process::def(const std::string& dll, const std::string& name, uint32_t arg_bytes,
                  std::function<uint32_t()> fn) {
    HostFunction f;
    f.name = dll + "!" + name;
    f.arg_bytes = arg_bytes;
    f.call = [this, key = f.name, fn = std::move(fn)](Cpu&) {
        ++call_counts[key];
        if (traced(key)) std::fprintf(stderr, "  appel %s\n", key.c_str());
        return fn();
    };
    functions_[f.name] = std::move(f);
}

uint32_t Process::host_function(const std::string& name, uint32_t arg_bytes, std::function<uint32_t()> fn) {
    HostFunction f;
    f.name = name;
    f.arg_bytes = arg_bytes;
    f.call = [this, name, fn = std::move(fn)](Cpu&) {
        ++call_counts[name];
        if (traced(name)) std::fprintf(stderr, "  appel %s\n", name.c_str());
        return fn();
    };
    return cpu_.add_host_function(std::move(f));
}

Desktop& Process::desktop() {
    if (!desktop_) desktop_ = std::make_unique<Desktop>();
    return *desktop_;
}

uint32_t Process::ticks() const {
    using namespace std::chrono;
    return static_cast<uint32_t>(100000 + duration_cast<milliseconds>(steady_clock::now() - start_).count());
}

void Process::bind_imports() {
    for (const PeImport& imp : image_.imports) {
        const std::string key = imp.dll + "!" + imp.function;
        auto found = functions_.find(key);
        HostFunction f;
        if (found != functions_.end()) {
            f = found->second;
        } else {
            f.name = key;
            const size_t at = imp.function.rfind('@');
            const bool silent = std::find(silent_dlls_.begin(), silent_dlls_.end(), imp.dll) != silent_dlls_.end();
            if (silent && at != std::string::npos && imp.function[0] == '_') {
                // Son ou vidéo pas encore réalisés : la fonction ne fait rien et renvoie 0.
                f.arg_bytes = static_cast<uint32_t>(std::stoul(imp.function.substr(at + 1)));
                f.call = [this, key, words = f.arg_bytes / 4](Cpu&) -> uint32_t {
                    if (++call_counts[key] == 1) std::fprintf(stderr, "[hôte] %s : sans effet pour l'instant\n", key.c_str());
                    if (traced(key)) {
                        std::fprintf(stderr, "  appel %s(", key.c_str());
                        for (uint32_t i = 0; i < words; ++i) std::fprintf(stderr, i ? ", %#x" : "%#x", arg(static_cast<int>(i)));
                        std::fprintf(stderr, ")\n");
                    }
                    return 0;
                };
            } else {
                // Fonction pas encore écrite : l'appeler arrête le programme en la nommant.
                f.call = [key](Cpu&) -> uint32_t { throw std::runtime_error("fonction système non fournie"); };
            }
        }
        cpu_.write_u32(imp.slot, cpu_.add_host_function(std::move(f)));
    }
}

uint32_t Process::alloc(uint32_t size, bool zero) {
    const uint32_t rounded = (std::max<uint32_t>(size, 1) + 15) & ~15u;
    uint32_t address = 0;
    auto reuse = free_blocks_.find(rounded);
    if (reuse != free_blocks_.end()) {
        address = reuse->second;
        free_blocks_.erase(reuse);
        if (zero) cpu_.fill(address, 0, rounded);
    } else {
        if (heap_next_ + rounded > kHeapBase + kHeapSize || heap_next_ + rounded < heap_next_) return 0;
        address = heap_next_;  // mémoire jamais utilisée : déjà à zéro
        heap_next_ += rounded;
    }
    blocks_[address] = size;
    return address;
}

void Process::release(uint32_t address) {
    auto found = blocks_.find(address);
    if (found == blocks_.end()) return;
    free_blocks_.emplace((std::max<uint32_t>(found->second, 1) + 15) & ~15u, address);
    blocks_.erase(found);
}

uint32_t Process::alloc_size(uint32_t address) const {
    auto found = blocks_.find(address);
    return found == blocks_.end() ? 0 : found->second;
}

uint32_t Process::alloc_string(const std::string& text) {
    const uint32_t address = alloc(static_cast<uint32_t>(text.size() + 1));
    cpu_.write_string(address, text);
    return address;
}

namespace {

void send_from(Process& p, std::shared_ptr<std::vector<Message>> list, size_t index, uint32_t result) {
    const Message& m = (*list)[index];
    p.cpu().call_guest(p.desktop().wndproc, {m.hwnd, m.message, m.wparam, m.lparam},
                       [&p, list, index, result](uint32_t) {
                           if (index + 1 < list->size()) send_from(p, list, index + 1, result);
                           return result;
                       });
}

}  // namespace

void Process::send_messages(std::vector<Message> messages, uint32_t result) {
    if (messages.empty() || !desktop().wndproc) return;
    send_from(*this, std::make_shared<std::vector<Message>>(std::move(messages)), 0, result);
}

void Process::exit(int code) {
    exit_code_ = code;
    cpu_.request_stop();
}

int Process::run() {
    uint32_t esp = kStackBase + kStackSize - 0x100;
    esp -= 4;
    cpu_.write_u32(esp, kExitAddress);
    cpu_.set_reg(Cpu::Reg::ESP, esp);
    cpu_.set_reg(Cpu::Reg::EBP, 0);
    cpu_.run(image_.entry_point, kExitAddress);
    return exit_code_;
}

}  // namespace zeus::win32
