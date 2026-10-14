// Écrit le désassemblage d'une plage d'adresses, y compris hors de toute fonction reconnue
// (procédure de fenêtre atteinte par une table de sauts, par exemple).
// Arguments : SORTIE.txt DEBUT FIN   (adresses hexadécimales, FIN exclue)
import java.io.File;
import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;

public class Listing extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 3) {
            throw new IllegalArgumentException("usage : Listing.java SORTIE.txt DEBUT FIN");
        }
        Address start = toAddr(Long.parseLong(args[1], 16));
        Address end = toAddr(Long.parseLong(args[2], 16));
        try (PrintWriter w = new PrintWriter(new File(args[0]), "UTF-8")) {
            Address a = start;
            while (a.compareTo(end) < 0) {
                Instruction ins = getInstructionAt(a);
                if (ins == null) {
                    disassemble(a);
                    ins = getInstructionAt(a);
                }
                if (ins == null) {
                    w.printf("%s  ?? %02x%n", a, getByte(a) & 0xff);
                    a = a.add(1);
                    continue;
                }
                w.printf("%s  %s%n", a, ins);
                a = a.add(ins.getLength());
            }
        }
    }
}
