// OpenBRCM tier-2 RE: decompile one or more functions by name.
// Usage: scripts/ghidra_headless.sh Decompile.java wlc_phy_anacore [wlc_phy_attach ...]
// Read-only. GhidraScript (Java); prints decompiled C to stdout.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;

public class Decompile extends GhidraScript {
    public void run() throws Exception {
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        FunctionManager fm = currentProgram.getFunctionManager();
        for (String name : getScriptArgs()) {
            Function f = null;
            FunctionIterator it = fm.getFunctions(true);
            while (it.hasNext()) { Function x = it.next(); if (x.getName().equals(name)) { f = x; break; } }
            println("===== " + name + " @ " + (f == null ? "?" : f.getEntryPoint().toString()) + " =====");
            if (f == null) { println("(not found; address form: 0xADDR not supported here)"); continue; }
            DecompileResults r = di.decompileFunction(f, 120, monitor);
            if (r == null || !r.decompileCompleted()) { println("(decompile failed)"); continue; }
            println(r.getDecompiledFunction().getC());
        }
    }
}
