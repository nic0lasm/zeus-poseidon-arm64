// Extras du second parcours de FUN_00458170 réécrits pour l'hôte (native_sanctuaries.cpp) : les
// sanctuaires et monuments (FUN_0053e5f0) et les deux tours des murailles (FUN_0041fd40).
#pragma once

#include <cstdint>

#include "win32/cpu.h"

namespace zeus::win32 {

class Process;

// Vrai quand FUN_0053e5f0(bâtiment, x, y) serait traitée en natif ; n'écrit rien.
bool sanctuary_handled(Cpu& cpu, int32_t building);
// FUN_0053e5f0(this = 0x00FE5100, bâtiment, x, y), dans un cas où sanctuary_handled est vrai :
// rien n'est dessiné, seul l'objet de la classe de la partie reçoit le bâtiment (x et y inutiles).
void draw_sanctuary(Cpu& cpu, int32_t building);

// Les deux tours d'un bâtiment de type 0x81, 0x91 ou 0x92 (0x004593B1 à 0x004594F1), en mode de
// vue 0, vue non tournée : `first` et `second` sont les cases voisines de la rangée précédente,
// (x, y) la position de la case. towers_handled dit, sans rien écrire, si le code des tours est
// celui attendu et si le répartiteur natif dessinera chaque tour ; draw_towers les dessine alors.
bool towers_handled(Cpu& cpu, int32_t first, int32_t second, int32_t x, int32_t y);
void draw_towers(Cpu& cpu, int32_t first, int32_t second, int32_t x, int32_t y);
// Vérifie les empreintes et pose le chemin rapide de FUN_0053e5f0 ; renvoie le nombre de
// remplacements installés.
int install_native_sanctuaries(Process& process);

}  // namespace zeus::win32
