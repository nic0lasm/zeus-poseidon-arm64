#include "win32/cpu.h"

#include <unicorn/unicorn.h>

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
}

Cpu::~Cpu() {
    if (uc_) uc_close(uc_);
}

void Cpu::map(uint32_t address, uint32_t size) {
    const uint32_t start = address & ~(kPage - 1);
    const uint64_t end = (static_cast<uint64_t>(address) + size + kPage - 1) & ~static_cast<uint64_t>(kPage - 1);
    check(uc_mem_map(uc_, start, static_cast<size_t>(end - start), UC_PROT_ALL), "réservation mémoire");
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

void Cpu::on_thunk(uc_struct* uc, uint64_t address, uint32_t, void* user) {
    Cpu& cpu = *static_cast<Cpu*>(user);
    const size_t index = static_cast<size_t>((address - kThunkBase) / kThunkStride);
    if (index >= cpu.functions_.size()) {
        uc_emu_stop(uc);
        return;
    }
    const HostFunction& f = cpu.functions_[index];
    const uint32_t esp = cpu.reg(Reg::ESP);
    const uint32_t return_address = cpu.read_u32(esp);
    const uint32_t result = f.call(cpu);
    cpu.set_reg(Reg::EAX, result);
    cpu.set_reg(Reg::ESP, esp + 4 + f.arg_bytes);
    // Le changement de point d'exécution se fait en arrêtant puis en relançant l'émulation.
    cpu.redirect_ = true;
    cpu.redirect_to_ = return_address;
    uc_emu_stop(uc);
}

void Cpu::run(uint32_t start, uint32_t until) {
    uint32_t pc = start;
    while (pc != until) {
        redirect_ = false;
        const uc_err err = uc_emu_start(uc_, pc, until, 0, 0);
        if (err != UC_ERR_OK) {
            throw std::runtime_error(std::string("faute du programme émulé en ") +
                                     std::to_string(reg(Reg::EIP)) + " : " + uc_strerror(err));
        }
        if (!redirect_) break;
        pc = redirect_to_;
    }
}

}  // namespace zeus::win32
