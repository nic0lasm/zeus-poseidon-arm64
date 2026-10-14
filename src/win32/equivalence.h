// Banc d'équivalence : prouver qu'un hôte avec ses remplacements natifs fait exactement ce que fait
// le code d'origine, l'original émulé servant de juge (docs/equivalence.md).
//
//   - Empreinte d'état (zeus-run --state-log) : à chaque tour de simulation, une empreinte de
//     chaque zone de mémoire qui forme l'état de la partie. Deux exécutions --deterministic, l'une
//     sous --no-native, doivent écrire le même fichier (tools/check_scenarios.py).
//   - Vérification ciblée (zeus-run --verify) : à l'entrée d'une fonction du jeu, l'appel est
//     exécuté deux fois depuis le même état, remplacements natifs coupés puis actifs, et les
//     registres et la mémoire obtenus sont comparés. Arrêt et rapport à la première différence.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace zeus::win32 {

class Process;

// Zone de mémoire de l'état de la partie.
struct StateZone {
    int block;         // rang du bloc dans une sauvegarde, -1 si la zone n'y est pas
    uint32_t address;  // adresse dans Zeus.exe (GOG)
    uint32_t size;     // octets
};
// Toutes les zones, dans l'ordre de la sauvegarde ; `count` reçoit leur nombre.
const StateZone* state_zones(size_t& count);
// Nom lisible d'une zone (« bâtiments », « bloc 54 »…).
std::string zone_name(const StateZone& zone);

class StateLog;
class Verifier;

// Écrit dans `path` une ligne par tour de simulation : numéro du tour, puis l'empreinte de chaque
// zone avant le tour. La dernière ligne (« fin ») est l'état à la fin de l'exécution.
std::shared_ptr<StateLog> install_state_log(Process& process, const std::string& path);
void finish_state_log(StateLog& log);

// `target` : « ADRESSE » (une fonction, de son entrée à son retour) ou « DÉBUT:FIN » (un morceau de
// code, de DÉBUT jusqu'à FIN à la même hauteur de pile), en hexadécimal. Un appel sur `every` est
// vérifié. À installer avant les remplacements natifs : la vérification passe avant eux à l'entrée.
std::shared_ptr<Verifier> install_verifier(Process& process, const std::string& target, uint32_t every);
// Bilan sur la sortie d'erreur ; renvoie faux si une différence a été trouvée ou si un passage
// vérifié est resté inachevé.
bool report_verifier(Verifier& verifier);

}  // namespace zeus::win32
