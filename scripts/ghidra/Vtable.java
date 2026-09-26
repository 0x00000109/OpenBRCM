// OpenBRCM tier-2 RE: dump a function-pointer table / vtable by symbol name.
// Usage: scripts/ghidra_headless.sh Vtable.java dma64proc [sym ...] [--count N]
// Read-only. Resolves indirect/vtable call targets that re leaves as offsets.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;

public class Vtable extends GhidraScript {
    public void run() throws Exception {
        SymbolTable st = currentProgram.getSymbolTable();
        FunctionManager fm = currentProgram.getFunctionManager();
        Memory mem = currentProgram.getMemory();
        String[] args = getScriptArgs();
        int count = 0x140;
        for (int i = 0; i < args.length; i++) {
            if (args[i].equals("--count") && i + 1 < args.length) { count = Integer.decode(args[++i]); }
        }
        for (String nm : args) {
            if (nm.equals("--count")) continue;
            SymbolIterator it = st.getSymbols(nm);
            if (!it.hasNext()) { println("SYM " + nm + ": none"); continue; }
            Address a = it.next().getAddress();
            println("===== " + nm + " @ " + a + " =====");
            for (int off = 0; off <= count; off += 8) {
                try {
                    long v = mem.getLong(a.add(off));
                    Address ta = toAddr(v);
                    Function f = (ta == null) ? null : fm.getFunctionContaining(ta);
                    println(String.format("  +0x%03x -> 0x%x  %s", off, v, f == null ? "?" : f.getName()));
                } catch (Exception e) {
                    println(String.format("  +0x%03x <unreadable>", off));
                }
            }
        }
    }
}
