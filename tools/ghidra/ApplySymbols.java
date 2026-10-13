// Applique au programme courant les noms de analysis/notes/symbols.tsv (adresse, nom, preuve).
// Argument : chemin du fichier TSV. À lancer sans -readOnly pour que les noms soient enregistrés.
import java.io.BufferedReader;
import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class ApplySymbols extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) {
            throw new IllegalArgumentException("usage : ApplySymbols.java symbols.tsv");
        }
        int applied = 0;
        try (BufferedReader in = new BufferedReader(
                new InputStreamReader(new FileInputStream(args[0]), StandardCharsets.UTF_8))) {
            String line = in.readLine();  // en-tête
            while ((line = in.readLine()) != null) {
                String[] parts = line.split("\t");
                if (parts.length < 2 || parts[0].isEmpty()) {
                    continue;
                }
                Address a = toAddr(Long.parseLong(parts[0], 16));
                Function f = getFunctionAt(a);
                if (f != null) {
                    f.setName(parts[1], SourceType.USER_DEFINED);
                } else {
                    createLabel(a, parts[1], true, SourceType.USER_DEFINED);
                }
                applied++;
            }
        }
        println(applied + " noms appliqués");
    }
}
