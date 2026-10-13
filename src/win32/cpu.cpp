#include "win32/cpu.h"

#include <unicorn/unicorn.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>

namespace zeus::win32 {
namespace {

// Zone où pointent les tables d'import : y « exécuter » une adresse appelle une fonction hôte.
constexpr uint32_t kThunkBase = 0xF0000000;
constexpr uint32_t kThunkSize = 0x00010000;
constexpr uint32_t kThunkStride = 4;
constexpr uint32_t kPage = 0x1000;

void check(uc_err err, const char* what) {
    if (err != UC_ERR_OK) throw std::runtime_error(std::string(what) + " : " + uc_strerror(err));
}

int to_uc(Cpu::Reg r) {
    switch (r) {
        case Cpu::Reg::EAX: return UC_X86_REG_EAX;
        case Cpu::Reg::ECX: return UC_X86_REG_ECX;
        case Cpu::Reg::EDX: return UC_X86_REG_EDX;
        case Cpu::Reg::EBX: return UC_X86_REG_EBX;
        case Cpu::Reg::ESP: return UC_X86_REG_ESP;
        case Cpu::Reg::EBP: return UC_X86_REG_EBP;
        case Cpu::Reg::ESI: return UC_X86_REG_ESI;
        case Cpu::Reg::EDI: return UC_X86_REG_EDI;
        case Cpu::Reg::EIP: return UC_X86_REG_EIP;
    }
    return UC_X86_REG_INVALID;
}

}  // namespace

Cpu::Cpu() {
    check(uc_open(UC_ARCH_X86, UC_MODE_32, &uc_), "ouverture du processeur émulé");
    check(uc_mem_map(uc_, kThunkBase, kThunkSize, UC_PROT_READ | UC_PROT_EXEC), "zone des imports");
    uc_hook hook;
    check(uc_hook_add(uc_, &hook, UC_HOOK_CODE, reinterpret_cast<void*>(&Cpu::on_thunk), this,
                      kThunkBase, kThunkBase + kThunkSize - 1),
          "interception des imports");
    // La case 0 est réservée au retour des fonctions du programme appelées depuis l'hôte.
    functions_.push_back({"<retour d'appel>", 0, nullptr});
}

Cpu::~Cpu() {
    if (uc_) uc_close(uc_);
}

void Cpu::map(uint32_t address, uint32_t size, bool executable) {
    const uint32_t start = address & ~(kPage - 1);
    const uint64_t end = (static_cast<uint64_t>(address) + size + kPage - 1) & ~static_cast<uint64_t>(kPage - 1);
    check(uc_mem_map(uc_, start, static_cast<size_t>(end - start),
                     executable ? UC_PROT_READ | UC_PROT_EXEC : UC_PROT_READ | UC_PROT_WRITE),
          "réservation mémoire");
}

void Cpu::write(uint32_t address, const void* data, size_t size) {
    check(uc_mem_write(uc_, address, data, size), "écriture mémoire");
}

void Cpu::read(uint32_t address, void* data, size_t size) const {
    check(uc_mem_read(uc_, address, data, size), "lecture mémoire");
}

uint32_t Cpu::read_u32(uint32_t address) const {
    uint8_t b[4];
    read(address, b, 4);
    return static_cast<uint32_t>(b[0]) | static_cast<uint32_t>(b[1]) << 8 |
           static_cast<uint32_t>(b[2]) << 16 | static_cast<uint32_t>(b[3]) << 24;
}

void Cpu::write_u32(uint32_t address, uint32_t value) {
    const uint8_t b[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                          static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
    write(address, b, 4);
}

uint16_t Cpu::read_u16(uint32_t address) const {
    uint8_t b[2];
    read(address, b, 2);
    return static_cast<uint16_t>(b[0] | b[1] << 8);
}

void Cpu::write_u16(uint32_t address, uint16_t value) {
    const uint8_t b[2] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
    write(address, b, 2);
}

uint8_t Cpu::read_u8(uint32_t address) const {
    uint8_t b = 0;
    read(address, &b, 1);
    return b;
}

void Cpu::write_u8(uint32_t address, uint8_t value) { write(address, &value, 1); }

std::string Cpu::read_string(uint32_t address, size_t max) const {
    std::string s;
    char chunk[64];
    while (s.size() < max) {
        read(address + static_cast<uint32_t>(s.size()), chunk, sizeof chunk);
        for (char c : chunk) {
            if (!c) return s;
            s.push_back(c);
        }
    }
    return s;
}

void Cpu::write_string(uint32_t address, const std::string& text) {
    write(address, text.c_str(), text.size() + 1);
}

void Cpu::fill(uint32_t address, uint8_t value, size_t size) {
    const std::vector<uint8_t> block(std::min<size_t>(size, 1 << 16), value);
    while (size) {
        const size_t n = std::min(size, block.size());
        write(address, block.data(), n);
        address += static_cast<uint32_t>(n);
        size -= n;
    }
}

// Unicorn ne permet pas de fixer directement la base de FS en mode 32 bits : on installe une
// table de descripteurs de segments (GDT) comme le ferait un système, avec un segment de code,
// un segment de données, un segment FS de base choisie et un segment de pile de niveau 0.
void Cpu::set_fs_base(uint32_t address) {
    constexpr uint32_t kGdt = 0xF0020000;
    constexpr int kEntries = 32;
    auto descriptor = [](uint32_t base, uint32_t limit, bool code, int dpl) {
        uint8_t flags = 0x40;  // segment 32 bits
        if (limit > 0xFFFFF) {
            limit >>= 12;
            flags |= 0x80;  // limite comptée en pages
        }
        const uint8_t access = static_cast<uint8_t>(0x80 | dpl << 5 | 0x10 | (code ? 0x0B : 0x03));
        return std::array<uint8_t, 8>{
            static_cast<uint8_t>(limit), static_cast<uint8_t>(limit >> 8),
            static_cast<uint8_t>(base), static_cast<uint8_t>(base >> 8), static_cast<uint8_t>(base >> 16),
            access, static_cast<uint8_t>(flags | ((limit >> 16) & 0x0F)), static_cast<uint8_t>(base >> 24)};
    };
    if (!gdt_ready_) {
        check(uc_mem_map(uc_, kGdt, kPage, UC_PROT_READ | UC_PROT_WRITE), "table des segments");
        uc_x86_mmr gdtr{};
        gdtr.base = kGdt;
        gdtr.limit = kEntries * 8 - 1;
        check(uc_reg_write(uc_, UC_X86_REG_GDTR, &gdtr), "registre GDTR");
        write(kGdt + 14 * 8, descriptor(0, 0xFFFFF000, true, 3).data(), 8);
        write(kGdt + 15 * 8, descriptor(0, 0xFFFFF000, false, 3).data(), 8);
        write(kGdt + 17 * 8, descriptor(0, 0xFFFFF000, false, 0).data(), 8);
    }
    write(kGdt + 16 * 8, descriptor(address, 0xFFF, false, 3).data(), 8);
    // Le processeur émulé démarre au niveau 0 : la pile utilise le descripteur de niveau 0.
    const int ss = 17 * 8, cs = 14 * 8 | 3, ds = 15 * 8 | 3, fs = 16 * 8 | 3;
    if (!gdt_ready_) {
        check(uc_reg_write(uc_, UC_X86_REG_SS, &ss), "segment SS");
        check(uc_reg_write(uc_, UC_X86_REG_CS, &cs), "segment CS");
        check(uc_reg_write(uc_, UC_X86_REG_DS, &ds), "segment DS");
        check(uc_reg_write(uc_, UC_X86_REG_ES, &ds), "segment ES");
        gdt_ready_ = true;
    }
    check(uc_reg_write(uc_, UC_X86_REG_FS, &fs), "segment FS");
}

uint32_t Cpu::reg(Reg r) const {
    uint32_t value = 0;
    check(uc_reg_read(uc_, to_uc(r), &value), "lecture de registre");
    return value;
}

void Cpu::set_reg(Reg r, uint32_t value) {
    check(uc_reg_write(uc_, to_uc(r), &value), "écriture de registre");
}

uint32_t Cpu::add_host_function(HostFunction function) {
    if ((functions_.size() + 1) * kThunkStride > kThunkSize)
        throw std::runtime_error("trop de fonctions hôtes");
    functions_.push_back(std::move(function));
    return kThunkBase + static_cast<uint32_t>(functions_.size() - 1) * kThunkStride;
}

uint32_t Cpu::arg(int n) const {
    // À l'entrée d'une fonction : [ESP] = adresse de retour, puis les arguments.
    return read_u32(reg(Reg::ESP) + 4 + 4 * static_cast<uint32_t>(n));
}

void Cpu::call_guest(uint32_t address, std::vector<uint32_t> args, std::function<uint32_t(uint32_t)> then) {
    pending_.address = address;
    pending_.args = std::move(args);
    pending_.then = std::move(then);
    has_pending_ = true;
}

// Empile les arguments de l'appel demandé et une adresse de retour qui ramène dans l'hôte.
void Cpu::start_pending_call(uint32_t esp) {
    has_pending_ = false;
    for (auto it = pending_.args.rbegin(); it != pending_.args.rend(); ++it) {
        esp -= 4;
        write_u32(esp, *it);
    }
    esp -= 4;
    write_u32(esp, kThunkBase);  // la première case de la zone des imports sert de point de retour
    set_reg(Reg::ESP, esp);
    redirect_ = true;
    redirect_to_ = pending_.address;
}

void Cpu::on_thunk(uc_struct* uc, uint64_t address, uint32_t, void* user) {
    Cpu& cpu = *static_cast<Cpu*>(user);
    const size_t index = static_cast<size_t>((address - kThunkBase) / kThunkStride);
    uc_emu_stop(uc);  // tout changement de point d'exécution passe par un arrêt puis une relance
    try {
        if (index == 0) {
            // Retour d'une fonction du programme appelée depuis l'hôte.
            if (cpu.continuations_.empty()) throw std::runtime_error("retour d'appel inattendu");
            Continuation& c = cpu.continuations_.back();
            const uint32_t result = c.then(cpu.reg(Reg::EAX));
            if (cpu.stop_requested_) return;
            if (cpu.has_pending_) {
                c.then = cpu.pending_.then;
                cpu.start_pending_call(c.esp);
                return;
            }
            cpu.set_reg(Reg::EAX, result);
            cpu.set_reg(Reg::ESP, c.esp + 4 + c.arg_bytes);
            cpu.redirect_ = true;
            cpu.redirect_to_ = c.return_address;
            cpu.continuations_.pop_back();
            return;
        }
        if (index >= cpu.functions_.size()) throw std::runtime_error("saut vers une adresse d'import inconnue");
        const HostFunction& f = cpu.functions_[index];
        const uint32_t esp = cpu.reg(Reg::ESP);
        const uint32_t return_address = cpu.read_u32(esp);
        cpu.current_ = f.name;
        const uint32_t result = f.call(cpu);
        if (cpu.stop_requested_) return;
        if (cpu.has_pending_) {
            cpu.continuations_.push_back({return_address, esp, f.arg_bytes, cpu.pending_.then});
            cpu.start_pending_call(esp);
            return;
        }
        cpu.set_reg(Reg::EAX, result);
        cpu.set_reg(Reg::ESP, esp + 4 + f.arg_bytes);
        cpu.redirect_ = true;
        cpu.redirect_to_ = return_address;
    } catch (const std::exception& e) {
        // Une exception C++ ne doit pas traverser l'émulateur : on la reporte à run().
        cpu.host_error_ = cpu.current_ + " : " + e.what();
    }
}

void Cpu::run(uint32_t start, uint32_t until) {
    uint32_t pc = start;
    while (pc != until) {
        redirect_ = false;
        const uc_err err = uc_emu_start(uc_, pc, until, 0, 0);
        if (!host_error_.empty()) {
            const std::string message = host_error_;
            host_error_.clear();
            throw std::runtime_error(message);
        }
        if (err != UC_ERR_OK) {
            char where[32];
            std::snprintf(where, sizeof where, "%#010x", reg(Reg::EIP));
            throw std::runtime_error(std::string("faute du programme émulé en ") + where + " : " +
                                     uc_strerror(err));
        }
        if (stop_requested_ || !redirect_) break;
        pc = redirect_to_;
    }
}

}  // namespace zeus::win32
