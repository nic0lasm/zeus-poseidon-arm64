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

    enum class Reg { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI, EIP };
    uint32_t reg(Reg r) const;
    void set_reg(Reg r, uint32_t value);

    // Enregistre une fonction hôte et renvoie l'adresse émulée à placer dans une table d'import.
    uint32_t add_host_function(HostFunction function);
    // n-ième argument (à partir de 0) de la fonction hôte en cours d'appel.
    uint32_t arg(int n) const;

    // Exécute à partir de `start` jusqu'à atteindre `until`. Lève std::runtime_error sur une
    // faute (accès hors mémoire, instruction invalide).
    void run(uint32_t start, uint32_t until);
    uint64_t instructions_run() const { return instructions_; }

private:
    static void on_thunk(uc_struct* uc, uint64_t address, uint32_t size, void* user);

    uc_struct* uc_ = nullptr;
    std::vector<HostFunction> functions_;
    bool redirect_ = false;
    uint32_t redirect_to_ = 0;
    uint64_t instructions_ = 0;
};

}  // namespace zeus::win32
