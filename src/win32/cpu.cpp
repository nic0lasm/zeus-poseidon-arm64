#include "win32/cpu.h"

#include <cstring>
#include <ctime>
#include <sys/mman.h>

#include <unicorn/unicorn.h>
#include <cstdlib>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <stdexcept>

namespace zeus::win32 {
namespace {

// Zone où pointent les tables d'import : y « exécuter » une adresse appelle une fonction hôte.
constexpr uint32_t kThunkBase = 0xF0000000;
constexpr uint32_t kThunkSize = 0x00010000;
constexpr uint32_t kThunkStride = 4;
// Tremplins des chemins rapides (add_fast_path) : code écrit par l'hôte, juste après les imports.
constexpr uint32_t kTrampolineBase = kThunkBase + kThunkSize, kTrampolineSize = 0x1000;
constexpr uint32_t kPage = 0x1000;
constexpr size_t kAddressSpace = size_t{1} << 32;

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
    // Modèle de processeur 32 bits qui connaît les pages non exécutables (voir enable_paging).
    check(uc_ctl_set_cpu_model(uc_, UC_CPU_X86_N270), "modèle de processeur");
    // Les 4 Go d'adresses du programme, réservés d'un bloc (les pages ne sont réellement
    // allouées qu'à leur première utilisation) : l'hôte et l'émulateur partagent la même
    // mémoire, et une fonction de l'hôte y accède par simple addition.
    void* block = mmap(nullptr, kAddressSpace, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    if (block == MAP_FAILED) throw std::runtime_error("réservation de l'espace d'adresses impossible");
    memory_ = static_cast<uint8_t*>(block);
    map_region(kThunkBase, kThunkSize, UC_PROT_READ | UC_PROT_EXEC, "zone des imports");
    code_ranges_.push_back({kThunkBase, kThunkBase + kThunkSize});
    uc_hook hook;
    check(uc_hook_add(uc_, &hook, UC_HOOK_CODE, reinterpret_cast<void*>(&Cpu::on_thunk), this,
                      kThunkBase, kThunkBase + kThunkSize - 1),
          "interception des imports");
    map_region(kTrampolineBase, kTrampolineSize, UC_PROT_READ | UC_PROT_EXEC, "zone des tremplins");
    code_ranges_.push_back({kTrampolineBase, kTrampolineBase + kTrampolineSize});
    trampoline_next_ = kTrampolineBase;
    // La case 0 est réservée au retour des fonctions du programme appelées depuis l'hôte.
    functions_.push_back({"<retour d'appel>", 0, nullptr});
}

Cpu::~Cpu() {
    if (uc_) uc_close(uc_);
    if (memory_) munmap(memory_, kAddressSpace);
}

void Cpu::map_region(uint32_t start, size_t size, uint32_t permissions, const char* what) {
    check(uc_mem_map_ptr(uc_, start, size, permissions, memory_ + start), what);
}

void Cpu::map(uint32_t address, uint32_t size, bool executable) {
    const uint32_t start = address & ~(kPage - 1);
    const uint64_t end = (static_cast<uint64_t>(address) + size + kPage - 1) & ~static_cast<uint64_t>(kPage - 1);
    map_region(start, static_cast<size_t>(end - start),
               executable ? UC_PROT_READ | UC_PROT_EXEC : UC_PROT_READ | UC_PROT_WRITE, "réservation mémoire");
    if (executable) code_ranges_.push_back({start, static_cast<uint32_t>(end)});
}

// Active la pagination, avec une table qui fait correspondre chaque adresse à elle-même et
// marque non exécutable tout ce qui n'est pas du code.
//
// Sans pagination, Unicorn tient toute page pour exécutable et fait passer chaque écriture
// mémoire par un chemin lent qui surveille le code modifié : une écriture coûte alors une
// trentaine de nanosecondes, contre moins d'une pour les autres instructions.
//
// Les pages non exécutables demandent Unicorn 2.1 : avant, la première page marquée ainsi fait
// fauter le programme émulé (vu avec 2.0.1, celui d'Ubuntu 24.04). On s'en passe alors, au prix de
// la lenteur décrite ci-dessus.
void Cpu::enable_paging() {
    // uc_version() : majeur << 24 | mineur << 16 | correctif << 8 | extra (UC_MAKE_VERSION varie selon les versions).
    if (uc_version(nullptr, nullptr) < (2u << 24 | 1u << 16)) {
        paging_ = true;
        return;
    }
    constexpr uint32_t kTables = 0xF0040000, kLarge = 0x200000;
    constexpr uint64_t kPresent = 1, kWritable = 2, kUser = 4, kLargePage = 0x80, kNoExecute = 1ull << 63;
    std::vector<uint64_t> tables(5 * 512, 0);  // PDPT, puis quatre répertoires de 512 entrées
    auto is_code = [this](uint64_t start, uint64_t end) {
        for (const auto& [a, b] : code_ranges_)
            if (start < b && a < end) return true;
        return false;
    };
    for (uint64_t i = 0; i < 4; ++i) tables[i] = (kTables + (i + 1) * kPage) | kPresent;
    for (uint64_t region = 0; region < 2048; ++region) {
        const uint64_t base = region * kLarge;
        uint64_t& entry = tables[512 + region];
        if (!is_code(base, base + kLarge)) {
            entry = base | kPresent | kWritable | kUser | kLargePage | kNoExecute;
            continue;
        }
        // Zone qui contient du code : une table de pages de 4 Ko, pour ne laisser exécutable
        // que le code lui-même.
        const uint64_t table = kTables + (tables.size() / 512) * kPage;
        entry = table | kPresent | kWritable | kUser;
        const size_t first = tables.size();
        tables.resize(first + 512);
        for (uint64_t page = 0; page < 512; ++page) {
            const uint64_t address = base + page * kPage;
            tables[first + page] = address | kPresent | kWritable | kUser |
                                   (is_code(address, address + kPage) ? 0 : kNoExecute);
        }
    }
    map_region(kTables, (tables.size() * 8 + kPage - 1) & ~static_cast<size_t>(kPage - 1), UC_PROT_READ | UC_PROT_WRITE,
               "tables de pages");
    write(kTables, tables.data(), tables.size() * 8);
    const uint32_t cr3 = kTables;
    uint32_t cr4 = 0, cr0 = 0;
    check(uc_reg_read(uc_, UC_X86_REG_CR4, &cr4), "registre CR4");
    check(uc_reg_read(uc_, UC_X86_REG_CR0, &cr0), "registre CR0");
    cr4 |= 1u << 5;                // PAE
    cr0 |= 1u | 1u << 31;          // mode protégé, pagination
    uc_x86_msr efer{};
    efer.rid = 0xC0000080;
    efer.value = 1u << 11;         // pages non exécutables
    check(uc_reg_write(uc_, UC_X86_REG_CR3, &cr3), "registre CR3");
    check(uc_reg_write(uc_, UC_X86_REG_CR4, &cr4), "registre CR4");
    check(uc_reg_write(uc_, UC_X86_REG_MSR, &efer), "registre EFER");
    check(uc_reg_write(uc_, UC_X86_REG_CR0, &cr0), "registre CR0");
    paging_ = true;
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
        map_region(kGdt, kPage, UC_PROT_READ | UC_PROT_WRITE, "table des segments");
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
    // La case de la fonction contient « ret n » : une fois la fonction hôte exécutée, c'est le
    // programme lui-même qui revient à son appelant, sans arrêt de l'émulateur.
    const uint32_t address = kThunkBase + static_cast<uint32_t>(functions_.size()) * kThunkStride;
    const uint8_t code[3] = {0xC2, static_cast<uint8_t>(function.arg_bytes), static_cast<uint8_t>(function.arg_bytes >> 8)};
    std::memcpy(host(address), code, sizeof code);
    functions_.push_back(std::move(function));
    return address;
}

uint32_t Cpu::arg(int n) const {
    // À l'entrée d'une fonction : [ESP] = adresse de retour, puis les arguments.
    return read_u32(reg(Reg::ESP) + 4 + 4 * static_cast<uint32_t>(n));
}

void Cpu::start_profile() {
    uc_hook hook;
    check(uc_hook_add(uc_, &hook, UC_HOOK_BLOCK, reinterpret_cast<void*>(&Cpu::on_block), this, 1, 0), "profil");
}

void Cpu::on_block(uc_struct*, uint64_t address, uint32_t, void* user) {
    Cpu& cpu = *static_cast<Cpu*>(user);
    const uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    if (cpu.profile_block_) {
        BlockTime& block = cpu.profile_[cpu.profile_block_];
        block.nanoseconds += now - cpu.profile_since_;
        ++block.executions;
    }
    cpu.profile_block_ = static_cast<uint32_t>(address);
    cpu.profile_since_ = now;
}

void Cpu::replace_function(uint32_t address, uint32_t arg_bytes, std::function<uint32_t(Cpu&)> fn) {
    // « ret » ou « ret n » à la place du premier octet de la fonction.
    const uint8_t code[3] = {static_cast<uint8_t>(arg_bytes ? 0xC2 : 0xC3), static_cast<uint8_t>(arg_bytes),
                             static_cast<uint8_t>(arg_bytes >> 8)};
    std::memcpy(host(address), code, arg_bytes ? 3 : 1);
    add_code_hook(address, [fn = std::move(fn)](Cpu& cpu) { cpu.set_reg(Reg::EAX, fn(cpu)); });
}

void Cpu::add_fast_path(uint32_t address, uint32_t arg_bytes, uint32_t displaced,
                        std::function<std::optional<uint32_t>(Cpu&)> fn) {
    if (displaced < 5 || displaced > 16) throw std::runtime_error("chemin rapide : octets déplacés hors limites");
    // Données (indicateur « traité » puis valeur de retour), puis le code du tremplin.
    const uint32_t data = trampoline_next_, code = data + 8;
    const uint32_t size = 8 + 0x11 + displaced + 5;
    if (data + size > kTrampolineBase + kTrampolineSize) throw std::runtime_error("trop de chemins rapides");
    trampoline_next_ = (data + size + 15) & ~15u;
    auto put32 = [](uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); };
    uint8_t* t = host(code);
    t[0] = 0x80; t[1] = 0x3D; put32(t + 2, data); t[6] = 0x00;  // cmp byte [indicateur], 0
    t[7] = 0x74; t[8] = 0x08;                                    // je vers le code d'origine
    t[9] = 0xA1; put32(t + 10, data + 4);                        // mov eax, [valeur]
    if (arg_bytes) {
        t[14] = 0xC2; t[15] = static_cast<uint8_t>(arg_bytes); t[16] = static_cast<uint8_t>(arg_bytes >> 8);
    } else {
        t[14] = 0xC3; t[15] = 0x90; t[16] = 0x90;
    }
    std::memcpy(t + 0x11, host(address), displaced);              // instructions déplacées
    t[0x11 + displaced] = 0xE9;                                   // jmp vers la suite de la fonction
    put32(t + 0x12 + displaced, (address + displaced) - (code + 0x11 + displaced + 5));
    uint8_t* entry = host(address);
    entry[0] = 0xE9;
    put32(entry + 1, code - (address + 5));
    std::memset(entry + 5, 0x90, displaced - 5);
    add_code_hook(code, [fn = std::move(fn), data](Cpu& cpu) {
        const std::optional<uint32_t> result = fn(cpu);
        uint8_t* d = cpu.host(data);
        d[0] = result ? 1 : 0;
        if (result) std::memcpy(d + 4, &*result, 4);
    });
}

void Cpu::add_code_hook(uint32_t address, std::function<void(Cpu&)> fn) {
    const auto found = code_hooks_.find(address);
    if (found != code_hooks_.end()) {
        // Une seule interception Unicorn par adresse : les fonctions s'enchaînent.
        found->second = [first = std::move(found->second), then = std::move(fn)](Cpu& cpu) {
            first(cpu);
            then(cpu);
        };
        return;
    }
    code_hooks_[address] = std::move(fn);
    uc_hook hook;
    check(uc_hook_add(uc_, &hook, UC_HOOK_CODE, reinterpret_cast<void*>(&Cpu::on_code_hook), this, address, address),
          "interception de code");
}

void Cpu::on_code_hook(uc_struct*, uint64_t address, uint32_t, void* user) {
    Cpu& cpu = *static_cast<Cpu*>(user);
    const auto found = cpu.code_hooks_.find(static_cast<uint32_t>(address));
    if (found == cpu.code_hooks_.end()) return;
    try {
        found->second(cpu);
    } catch (const std::exception& e) {
        // Une exception C++ ne doit pas traverser l'émulateur : on la reporte à run().
        char where[32];
        std::snprintf(where, sizeof where, "%#010x", static_cast<uint32_t>(address));
        cpu.host_error_ = std::string("code du jeu en ") + where + " : " + e.what();
        uc_emu_stop(cpu.uc_);
    }
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
    // Le cas courant n'arrête pas l'émulateur : la fonction hôte s'exécute, puis le « ret n » de
    // la case rend la main. Un changement de point d'exécution (appel du programme depuis
    // l'hôte, fin, erreur) passe en revanche par un arrêt puis une relance.
    try {
        if (index == 0) {
            uc_emu_stop(uc);
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
        if (cpu.stop_requested_) {
            uc_emu_stop(uc);
            return;
        }
        if (cpu.has_pending_) {
            uc_emu_stop(uc);
            cpu.continuations_.push_back({return_address, esp, f.arg_bytes, cpu.pending_.then});
            cpu.start_pending_call(esp);
            return;
        }
        cpu.set_reg(Reg::EAX, result);
    } catch (const std::exception& e) {
        // Une exception C++ ne doit pas traverser l'émulateur : on la reporte à run().
        cpu.host_error_ = cpu.current_ + " : " + e.what();
        uc_emu_stop(uc);
    }
}

void Cpu::run(uint32_t start, uint32_t until) {
    if (!paging_) enable_paging();
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
