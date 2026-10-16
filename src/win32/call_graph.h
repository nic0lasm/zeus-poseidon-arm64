// Arbre d'appels du jeu émulé (zeus-run --call-graph) : qui appelle qui, combien de fois, et le
// temps passé dans chaque bloc de code, séparé selon qu'il s'exécute pendant un tour de
// simulation (FUN_00513a30) ou en dehors. tools/simulation_map.py en tire la carte de la
// simulation (docs/engine/simulation.md).
#pragma once

#include <memory>
#include <string>

namespace zeus::win32 {

class Process;
class CallGraph;

std::shared_ptr<CallGraph> install_call_graph(Process& process, const std::string& path);
void finish_call_graph(CallGraph& graph);

// Durées mesurées sans ralentir l'émulation (zeus-run --time-span) : « DÉBUT:RETOUR,… » en
// hexadécimal, l'entrée d'une fonction et l'adresse où elle rend la main à son appelant. Seules
// ces deux adresses sont interceptées : le temps réel d'une partie n'en est presque pas changé.
class TimeSpans;
std::shared_ptr<TimeSpans> install_time_spans(Process& process, const std::string& spec);
void report_time_spans(const TimeSpans& spans);

}  // namespace zeus::win32
