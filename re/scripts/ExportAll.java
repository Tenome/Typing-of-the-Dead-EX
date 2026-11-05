// Exports decompiled C, function index, call graph, data references, imports and exports.
// @category TotD
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

public class ExportAll extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        File root = new File(args.length > 0 ? args[0] : System.getProperty("user.dir"), currentProgram.getName());
        root.mkdirs();
        exportImportsAndExports(root);
        exportFunctionsAndDecompilation(root);
        exportDataReferences(root);
        println("Exported " + currentProgram.getName() + " to " + root);
    }

    private PrintWriter open(File dir, String name) throws IOException {
        return new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, name)), StandardCharsets.UTF_8));
    }

    private void exportImportsAndExports(File dir) throws IOException {
        SymbolTable symbols = currentProgram.getSymbolTable();
        try (PrintWriter out = open(dir, "imports.tsv")) {
            for (Symbol s : symbols.getExternalSymbols()) {
                out.println(s.getParentNamespace().getName() + "\t" + s.getName() + "\t" + s.getAddress());
            }
        }
        try (PrintWriter out = open(dir, "exports.tsv")) {
            AddressIterator it = symbols.getExternalEntryPointIterator();
            while (it.hasNext()) {
                Address a = it.next();
                Symbol s = symbols.getPrimarySymbol(a);
                out.println(a + "\t" + (s == null ? "" : s.getName()));
            }
        }
    }

    private String joinNames(Set<Function> functions) {
        StringBuilder sb = new StringBuilder();
        for (Function f : functions) {
            if (sb.length() > 0) sb.append(',');
            sb.append(f.getName()).append('@').append(f.getEntryPoint());
        }
        return sb.toString();
    }

    private void exportFunctionsAndDecompilation(File dir) throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.setOptions(new DecompileOptions());
        decompiler.toggleCCode(true);
        decompiler.openProgram(currentProgram);
        FunctionManager functions = currentProgram.getFunctionManager();
        try (PrintWriter index = open(dir, "functions.tsv"); PrintWriter code = open(dir, "decompiled.c")) {
            index.println("entry\tname\tsize\tcallers\tcallees");
            for (Function f : functions.getFunctions(true)) {
                if (monitor.isCancelled()) break;
                Set<Function> callers = f.getCallingFunctions(monitor);
                Set<Function> callees = f.getCalledFunctions(monitor);
                index.println(f.getEntryPoint() + "\t" + f.getName() + "\t" + f.getBody().getNumAddresses()
                        + "\t" + joinNames(callers) + "\t" + joinNames(callees));
                if (f.isExternal() || f.isThunk()) continue;
                code.println("// ==== " + f.getName() + " @ " + f.getEntryPoint() + " | callers: " + joinNames(callers));
                DecompileResults result = decompiler.decompileFunction(f, 60, monitor);
                if (result != null && result.decompileCompleted()) {
                    code.println(result.getDecompiledFunction().getC());
                } else {
                    code.println("// decompile failed: " + (result == null ? "null" : result.getErrorMessage()));
                }
            }
        }
        decompiler.dispose();
    }

    private void exportDataReferences(File dir) throws IOException {
        ReferenceManager refs = currentProgram.getReferenceManager();
        FunctionManager functions = currentProgram.getFunctionManager();
        try (PrintWriter out = open(dir, "data_refs.tsv")) {
            out.println("from\tfunction\tto\ttype");
            AddressIterator sources = refs.getReferenceSourceIterator(currentProgram.getMinAddress(), true);
            while (sources.hasNext()) {
                Address from = sources.next();
                for (Reference r : refs.getReferencesFrom(from)) {
                    if (!r.getReferenceType().isData() || !r.getToAddress().isMemoryAddress()) continue;
                    Function f = functions.getFunctionContaining(from);
                    out.println(from + "\t" + (f == null ? "" : f.getName()) + "\t" + r.getToAddress() + "\t" + r.getReferenceType());
                }
            }
        }
    }
}
