// OpenBRCM tier-2 RE: list references to one or more symbols (functions/data).
// Usage: scripts/ghidra_headless.sh Refs.java wlc_phy_btc_adjust_acphy [sym ...]
// Read-only. Resolves "who installs/calls this function?" that re may miss.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;

public class Refs extends GhidraScript {
    public void run() throws Exception {
        SymbolTable st = currentProgram.getSymbolTable();
        ReferenceManager rm = currentProgram.getReferenceManager();
        FunctionManager fm = currentProgram.getFunctionManager();
        for (String nm : getScriptArgs()) {
            SymbolIterator it = st.getSymbols(nm);
            if (!it.hasNext()) { println("SYM " + nm + ": none"); continue; }
            Symbol s = it.next();
            Address a = s.getAddress();
            println("SYM " + nm + " @ " + a);
            ReferenceIterator ri = rm.getReferencesTo(a);
            int n = 0;
            while (ri.hasNext() && n < 50) {
                Reference r = ri.next();
                Function f = fm.getFunctionContaining(r.getFromAddress());
                println("   ref from " + r.getFromAddress() + " (" + (f == null ? "?" : f.getName()) + ") type=" + r.getReferenceType());
                n++;
            }
            if (n == 0) println("   (no references)");
        }
    }
}
