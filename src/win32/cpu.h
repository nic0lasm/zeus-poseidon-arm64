// Processeur x86 32 bits émulé, dans lequel tourne le code d'origine du jeu.
//
// Le code émulé ne voit que la mémoire qu'on lui donne et ne peut agir sur la machine qu'à
// travers les fonctions hôtes enregistrées ici : c'est ce cloisonnement qui autorise à
// exécuter le code du jeu. Repose sur Unicorn (licence GPL-2.0).
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

struct uc_struct;

namespace zeus::win32 {

class Cpu;

// Fonction hôte appelée à la place d'une fonction importée par le programme émulé.
// Elle lit ses arguments avec Cpu::arg et renvoie la valeur de EAX. `arg_bytes` est le nombre
// d'octets d'arguments à dépiler (convention stdcall) ; 0 pour une fonction cdecl.
struct HostFunction {
    std::string name;
    uint32_t arg_bytes = 0;
    std::function<uint32_t(Cpu&)> call;
};

class Cpu {
public:
    Cpu();
    ~Cpu();
    Cpu(const Cpu&) = delete;
    Cpu& operator=(const Cpu&) = delete;

    // Réserve une zone de mémoire émulée (adresse et taille arrondies à la page).
    void map(uint32_t address, uint32_t size);
    void write(uint32_t address, const void* data, size_t size);
    void read(uint32_t address, void* data, size_t size) const;
    uint32_t read_u32(uint32_t address) const;
    void write_u32(uint32_t address, uint32_t value);
    uint16_t read_u16(uint32_t address) const;
    void write_u16(uint32_t address, uint16_t value);
    uint8_t read_u8(uint32_t address) const;
    void write_u8(uint32_t address, uint8_t value);
    // Chaîne C terminée par un octet nul (au plus `max` caractères).
    std::string read_string(uint32_t address, size_t max = 4096) const;
    void write_string(uint32_t address, const std::string& text);  // avec l'octet nul final
    void fill(uint32_t address, uint8_t value, size_t size);
    // Base du segment FS : bloc d'environnement du fil d'exécution (TEB) sous Windows.
    void set_fs_base(uint32_t address);

    enum class Reg { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI, EIP };
    uint32_t reg(Reg r) const;
    void set_reg(Reg r, uint32_t value);

    // Enregistre une fonction hôte et renvoie l'adresse émulée à placer dans une table d'import.
    uint32_t add_host_function(HostFunction function);
    // n-ième argument (à partir de 0) de la fonction hôte en cours d'appel.
    uint32_t arg(int n) const;
    // Depuis une fonction hôte : fait appeler une fonction du programme émulé (convention
    // stdcall) avant de rendre la main. `then` reçoit la valeur renvoyée et fournit le résultat
    // de la fonction hôte ; elle peut à son tour demander un autre appel.
    void call_guest(uint32_t address, std::vector<uint32_t> args, std::function<uint32_t(uint32_t)> then);

    // Exécute à partir de `start` jusqu'à atteindre `until`. Lève std::runtime_error sur une
    // faute (accès hors mémoire, instruction invalide).
    void run(uint32_t start, uint32_t until);
    // À appeler depuis une fonction hôte pour arrêter l'exécution après son retour.
    void request_stop() { stop_requested_ = true; }
    bool stopped() const { return stop_requested_; }
    uint64_t instructions_run() const { return instructions_; }

private:
    static void on_thunk(uc_struct* uc, uint64_t address, uint32_t size, void* user);

    uc_struct* uc_ = nullptr;
    std::vector<HostFunction> functions_;
    struct PendingCall {
        uint32_t address = 0;
        std::vector<uint32_t> args;
        std::function<uint32_t(uint32_t)> then;
    };
    struct Continuation {
        uint32_t return_address = 0, esp = 0, arg_bytes = 0;
        std::function<uint32_t(uint32_t)> then;
    };
    void start_pending_call(uint32_t esp);

    bool has_pending_ = false;
    PendingCall pending_;
    std::vector<Continuation> continuations_;
    bool redirect_ = false;
    bool stop_requested_ = false;
    bool gdt_ready_ = false;
    std::string host_error_;
    std::string current_;
    uint32_t redirect_to_ = 0;
    uint64_t instructions_ = 0;
};

}  // namespace zeus::win32
