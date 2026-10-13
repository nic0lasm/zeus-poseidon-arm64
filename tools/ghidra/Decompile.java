// Décompile les fonctions contenant les adresses données et les écrit dans un fichier.
// Arguments : SORTIE.c ADRESSE [ADRESSE...]   (adresses hexadécimales)
import java.io.File;
import java.io.PrintWriter;
import java.util.LinkedHashSet;
import java.util.Set;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class Decompile extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            throw new IllegalArgumentException("usage : Decompile.java SORTIE.c ADRESSE...");
        }
        Set<Function> functions = new LinkedHashSet<>();
        try (PrintWriter w = new PrintWriter(new File(args[0]), "UTF-8")) {
            for (int i = 1; i < args.length; i++) {
                Address a = toAddr(Long.parseLong(args[i], 16));
                Function f = getFunctionContaining(a);
                if (f == null) {
                    w.printf("// %s : aucune fonction à cette adresse%n", a);
                } else {
                    functions.add(f);
                }
            }
            DecompInterface decomp = new DecompInterface();
            decomp.openProgram(currentProgram);
            try {
                for (Function f : functions) {
                    DecompileResults r = decomp.decompileFunction(f, 180, monitor);
                    w.printf("// ===== %s @ %s, %d octets, %d appelants =====%n", f.getName(),
                            f.getEntryPoint(), f.getBody().getNumAddresses(),
                            f.getCallingFunctions(monitor).size());
                    w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC()
                            : "// échec de la décompilation : " + r.getErrorMessage());
                }
            } finally {
                decomp.dispose();
            }
        }
    }
}
