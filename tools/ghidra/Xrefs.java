// Liste les références vers des adresses (ou des plages), avec la fonction qui les contient.
// Arguments : SORTIE.tsv ADRESSE[:LONGUEUR] ...   (hexadécimal ; LONGUEUR en octets, défaut 1)
import java.io.File;
import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

public class Xrefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            throw new IllegalArgumentException("usage : Xrefs.java SORTIE.tsv ADRESSE[:LONGUEUR]...");
        }
        ReferenceManager refs = currentProgram.getReferenceManager();
        try (PrintWriter w = new PrintWriter(new File(args[0]), "UTF-8")) {
            w.println("cible\tdepuis\ttype\tfonction\tentrée");
            for (int i = 1; i < args.length; i++) {
                String[] parts = args[i].split(":");
                Address start = toAddr(Long.parseLong(parts[0], 16));
                long length = parts.length > 1 ? Long.parseLong(parts[1], 16) : 1;
                AddressSet range = new AddressSet(start, start.add(length - 1));
                var targets = refs.getReferenceDestinationIterator(range, true);
                while (targets.hasNext() && !monitor.isCancelled()) {
                    Address target = targets.next();
                    ReferenceIterator it = refs.getReferencesTo(target);
                    while (it.hasNext()) {
                        Reference r = it.next();
                        Function f = getFunctionContaining(r.getFromAddress());
                        w.printf("%s\t%s\t%s\t%s\t%s%n", target, r.getFromAddress(),
                                r.getReferenceType(), f == null ? "-" : f.getName(),
                                f == null ? "-" : f.getEntryPoint().toString());
                    }
                }
            }
        }
    }
}
