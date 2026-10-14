// Exporte la carte du code du programme courant, en lecture seule : une ligne par fonction avec
// adresse, taille en octets, nombre d'instructions, appelés, imports appelés et chaînes citées.
// Aucun octet ni code du jeu : seulement des adresses, des compteurs et des chaînes de messages
// (tronquées) servant d'indices de classement. Argument : chemin du fichier de sortie.
import java.io.File;
import java.io.PrintWriter;
import java.util.LinkedHashSet;
import java.util.Set;
import java.util.TreeSet;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;

public class ExportCodeMap extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) {
            throw new IllegalArgumentException("usage : ExportCodeMap.java SORTIE.tsv");
        }
        try (PrintWriter w = new PrintWriter(new File(args[0]), "UTF-8")) {
            w.println("adresse\ttaille\tinstructions\tnom\tappelés\timports\tchaînes");
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Function f = it.next();
                if (f.isThunk() || f.isExternal()) {
                    continue;
                }
                Set<String> callees = new TreeSet<>();
                Set<String> imports = new TreeSet<>();
                Set<String> strings = new LinkedHashSet<>();
                int count = 0;
                InstructionIterator ii = currentProgram.getListing().getInstructions(f.getBody(), true);
                while (ii.hasNext()) {
                    Instruction in = ii.next();
                    count++;
                    for (Reference r : in.getReferencesFrom()) {
                        Address to = r.getToAddress();
                        if (r.getReferenceType().isCall()) {
                            Function g = getFunctionAt(to);
                            if (g == null) {
                                continue;
                            }
                            if (g.isThunk()) {
                                g = g.getThunkedFunction(true);
                            }
                            if (g.isExternal()) {
                                imports.add(g.getExternalLocation().getLibraryName() + "!" + g.getName());
                            } else {
                                callees.add(g.getEntryPoint().toString());
                            }
                        } else if (r.getReferenceType().isData() && strings.size() < 6) {
                            Data d = getDataAt(to);
                            if (d != null && d.hasStringValue()) {
                                String s = String.valueOf(d.getValue());
                                s = s.replaceAll("[\\t\\r\\n]", " ");
                                strings.add(s.length() > 60 ? s.substring(0, 60) : s);
                            }
                        }
                    }
                }
                w.printf("%s\t%d\t%d\t%s\t%s\t%s\t%s%n", f.getEntryPoint(), f.getBody().getNumAddresses(),
                        count, f.getName(), String.join(",", callees), String.join(",", imports),
                        String.join("|", strings));
            }
        }
    }
}
