// Exporte le pseudo-code C décompilé du programme courant dans un seul fichier.
// Arguments : SORTIE.c [adresse_min adresse_max]  (bornes hexadécimales facultatives)
import java.io.File;
import java.io.PrintWriter;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

public class ExportDecompiled extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1 && args.length != 3) {
            throw new IllegalArgumentException("usage : ExportDecompiled.java SORTIE.c [min max]");
        }
        long min = args.length == 3 ? Long.parseLong(args[1], 16) : 0;
        long max = args.length == 3 ? Long.parseLong(args[2], 16) : Long.MAX_VALUE;
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        try (PrintWriter w = new PrintWriter(new File(args[0]), "UTF-8")) {
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Function f = it.next();
                long a = f.getEntryPoint().getOffset();
                if (a < min || a > max || f.isThunk() || f.isExternal()) {
                    continue;
                }
                DecompileResults r = decomp.decompileFunction(f, 120, monitor);
                w.printf("// ===== %s @ %s, %d octets =====%n", f.getName(), f.getEntryPoint(),
                        f.getBody().getNumAddresses());
                if (r.decompileCompleted()) {
                    w.println(r.getDecompiledFunction().getC());
                } else {
                    w.println("// échec de la décompilation : " + r.getErrorMessage());
                }
            }
        } finally {
            decomp.dispose();
        }
    }
}
