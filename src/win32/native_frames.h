// Cadres des fenêtres et surbrillances en cases de 16 pixels, réécrits pour l'hôte
// (native_frames.cpp) : FUN_0059ee90, FUN_0059f2a0, FUN_0059f6e0 et la barre FUN_0059ed50.
#pragma once

#include "win32/cpu.h"

namespace zeus::win32 {

class Process;

// Vérifie les empreintes et pose les chemins rapides ; renvoie le nombre de remplacements installés.
int install_native_frames(Process& process);

}  // namespace zeus::win32
