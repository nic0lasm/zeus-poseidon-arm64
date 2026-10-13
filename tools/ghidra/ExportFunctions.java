// Exporte la liste des fonctions du programme courant en TSV.
// Argument : chemin du fichier de sortie.
import java.io.File;
import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

public class ExportFunctions extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) {
            throw new IllegalArgumentException("usage : ExportFunctions.java SORTIE.tsv");
        }
        try (PrintWriter w = new PrintWriter(new File(args[0]), "UTF-8")) {
            w.println("adresse\ttaille\tnom\tappelants");
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Function f = it.next();
                w.printf("%s\t%d\t%s\t%d%n", f.getEntryPoint(), f.getBody().getNumAddresses(),
                        f.getName(), f.getCallingFunctions(monitor).size());
            }
        }
    }
}
