// Animations et compléments des bâtiments, réécrits pour l'hôte (native_animations.cpp) : ce que
// le second parcours de la boucle de dessin (native_draw.cpp) leur demande.
#pragma once

#include <cstdint>

#include "win32/cpu.h"

namespace zeus::win32 {

class Process;

// 0x0045984B à 0x00459C88, case d'ancrage dont l'image du sol est animée (+0x1E de son entrée non
// nul) : FUN_00413380 (image suivante), puis FUN_0045d140 (son dessin). `building` est EBX
// (bâtiment de la case), `esp` celui de FUN_00458170. Renvoie faux sans avoir rien écrit quand
// le cas n'est pas traité ; sinon la case en est en 0x00459CE2, EBX inchangé.
bool animated_anchor(Cpu& cpu, uint32_t esp, int32_t tile, int32_t building, int32_t x, int32_t y);

// Remplacements (FUN_00413380, FUN_0045d140, FUN_0044f320) ; renvoie le nombre installé.
int install_native_animations(Process& process);

}  // namespace zeus::win32
