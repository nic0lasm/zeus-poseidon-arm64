// Écrit le p-code brut des fonctions contenant les adresses données : une ligne par instruction
// machine, suivie de ses opérations. Sert de matière première à la traduction en code natif.
// Arguments : SORTIE.txt ADRESSE [ADRESSE...]   (adresses hexadécimales, sans 0x)
import java.io.File;
import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.pcode.PcodeOp;
import ghidra.program.model.pcode.Varnode;

public class ExportPcode extends GhidraScript {
    private String name(Varnode v) {
        if (v == null) {
            return "-";
        }
        if (v.isRegister()) {
            var r = currentProgram.getRegister(v.getAddress(), v.getSize());
            return (r != null ? r.getName() : "reg" + Long.toHexString(v.getOffset())) + ":" + v.getSize();
        }
        if (v.isConstant()) {
            return "#" + Long.toHexString(v.getOffset()) + ":" + v.getSize();
        }
        if (v.isUnique()) {
            return "u" + Long.toHexString(v.getOffset()) + ":" + v.getSize();
        }
        return v.getAddress().getAddressSpace().getName() + "[" + Long.toHexString(v.getOffset()) + "]:" + v.getSize();
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        try (PrintWriter w = new PrintWriter(new File(args[0]), "UTF-8")) {
            for (int i = 1; i < args.length; i++) {
                Address a = toAddr(Long.parseLong(args[i], 16));
                Function f = getFunctionContaining(a);
                if (f == null) {
                    w.printf("; %s : aucune fonction%n", a);
                    continue;
                }
                w.printf("FONCTION %s %d%n", f.getEntryPoint(), f.getBody().getNumAddresses());
                InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
                while (it.hasNext()) {
                    Instruction ins = it.next();
                    w.printf("I %s %d %s%n", ins.getAddress(), ins.getLength(), ins.toString());
                    for (PcodeOp op : ins.getPcode()) {
                        StringBuilder sb = new StringBuilder("  ");
                        sb.append(name(op.getOutput())).append(" = ").append(op.getMnemonic());
                        for (Varnode in : op.getInputs()) {
                            sb.append(' ').append(name(in));
                        }
                        w.println(sb);
                    }
                }
            }
        }
    }
}
