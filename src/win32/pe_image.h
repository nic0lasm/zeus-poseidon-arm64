// Chargement d'un exécutable Windows 32 bits (PE) dans la mémoire du processeur émulé.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "win32/cpu.h"

namespace zeus::win32 {

struct PeSection {
    std::string name;
    uint32_t address = 0, virtual_size = 0, raw_size = 0;
};

// Une fonction importée : la case `slot` de la table d'import doit recevoir son adresse.
struct PeImport {
    std::string dll;       // en minuscules
    std::string function;  // nom, ou « #n » pour un import par numéro
    uint32_t slot = 0;
};

struct PeImage {
    uint32_t image_base = 0, entry_point = 0, image_size = 0;
    std::vector<PeSection> sections;
    std::vector<PeImport> imports;
};

// Lit le fichier, copie ses sections dans la mémoire émulée et relève ses imports.
// N'exécute rien. Lève std::runtime_error si le fichier n'est pas un PE 32 bits i386.
PeImage load_pe(const std::filesystem::path& path, Cpu& cpu);

}  // namespace zeus::win32
