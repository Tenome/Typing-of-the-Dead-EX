// Creates functions at the given addresses if missing and writes their decompilation to a file.
// Args: <output file> <program-relative hex address>...
// @category TotD
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.io.*;
import java.nio.charset.StandardCharsets;

public class DecompileAt extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(new OutputStreamWriter(new FileOutputStream(args[0], true), StandardCharsets.UTF_8))) {
            for (int i = 1; i < args.length; i++) {
                Address address = toAddr(Long.parseLong(args[i], 16));
                Function function = getFunctionAt(address);
                if (function == null) {
                    disassemble(address);
                    new CreateFunctionCmd(address).applyTo(currentProgram);
                    function = getFunctionAt(address);
                }
                if (function == null) {
                    out.println("// cannot create a function at " + address);
                    continue;
                }
                DecompileResults results = decompiler.decompileFunction(function, 120, monitor);
                out.println("// ==== " + currentProgram.getName() + " " + function.getName() + " @ " + address);
                out.println(results.decompileCompleted() ? results.getDecompiledFunction().getC() : "// failed: " + results.getErrorMessage());
            }
        }
        decompiler.dispose();
    }
}
