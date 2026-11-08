import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import java.io.*;
import java.util.*;

/**
 * Static research export for the bundled v3dmm 0.14 and 7gen 0.3b binaries.
 * Arguments: output directory, then optional function addresses to decompile.
 * Writes only to the selected output directory and the Ghidra project. Never
 * executes the analyzed program or modifies its original binary.
 * See docs/tools/v3dmm-research.md for inputs, commands and analysis limitations.
 */
public class ResearchDump extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0) {
            throw new IllegalArgumentException("Expected output directory followed by optional function addresses");
        }
        File dir = new File(args[0], currentProgram.getName());
        dir.mkdirs();
        // Ghidra 12.1.2 inferred no-return for some manager CRT free functions.
        // Restore fallthrough and function bodies before trusting decompilation.
        FunctionIterator fixes=currentProgram.getFunctionManager().getFunctions(true);
        while(fixes.hasNext()) {
            Function f=fixes.next();
            if(f.getName().equals("_free")) {
                f.setNoReturn(false);
                ReferenceIterator refs=currentProgram.getReferenceManager().getReferencesTo(f.getEntryPoint());
                while(refs.hasNext()) {
                    Reference r=refs.next();
                    Instruction ins=getInstructionAt(r.getFromAddress());
                    if(ins!=null && ins.getFlowType().isCall()) {
                        ins.setFlowOverride(FlowOverride.NONE);
                        disassemble(ins.getAddress().add(ins.getLength()));
                    }
                }
                println("Ensured _free has a fallthrough");
            }
        }
        DataIterator pointers=currentProgram.getListing().getDefinedData(true);
        while(pointers.hasNext()) {
            Data d=pointers.next();
            if(d.isPointer() && d.getValue() instanceof Address) {
                Address a=(Address)d.getValue();
                ghidra.program.model.mem.MemoryBlock block=currentProgram.getMemory().getBlock(a);
                if(block!=null && block.isExecute() && getFunctionContaining(a)==null) {
                    disassemble(a);
                    createFunction(a,null);
                }
            }
        }
        FunctionIterator bodies=currentProgram.getFunctionManager().getFunctions(true);
        while(bodies.hasNext()) CreateFunctionCmd.fixupFunctionBody(currentProgram,bodies.next(),monitor);
        Set<Function> chosen = new TreeSet<>((a,b)->a.getEntryPoint().compareTo(b.getEntryPoint()));
        try (PrintWriter out = new PrintWriter(new File(dir,"index.txt"))) {
            out.println("PROGRAM " + currentProgram.getName() + " BASE " + currentProgram.getImageBase());
            SymbolIterator syms = currentProgram.getSymbolTable().getAllSymbols(true);
            while(syms.hasNext()) {
                Symbol s = syms.next();
                if(s.isExternal() || s.isExternalEntryPoint()) out.println("SYMBOL " + s.getAddress()+" "+s.getName(true));
            }
            FunctionIterator funcs = currentProgram.getFunctionManager().getFunctions(true);
            while(funcs.hasNext()) {
                Function f=funcs.next();
                out.println("FUNCTION " + f.getEntryPoint()+" "+f.getName()+" size="+f.getBody().getNumAddresses()+" noreturn="+f.hasNoReturn());
                if(currentProgram.getName().equalsIgnoreCase("VFS.dll") || currentProgram.getName().endsWith(".pyd")) chosen.add(f);
            }
            DataIterator data=currentProgram.getListing().getDefinedData(true);
            while(data.hasNext()) {
                Data d=data.next();
                if(!(d.getValue() instanceof String)) continue;
                String value=(String)d.getValue();
                out.print("STRING "+d.getAddress()+" "+value.replace("\n","\\n").replace("\r","\\r"));
                ReferenceIterator refs=currentProgram.getReferenceManager().getReferencesTo(d.getAddress());
                while(refs.hasNext()) {
                    Reference r=refs.next();
                    Function f=currentProgram.getFunctionManager().getFunctionContaining(r.getFromAddress());
                    out.print(" | "+r.getFromAddress()+" "+(f==null?"?":f.getName()));
                    if(f!=null && value.toLowerCase().matches("(?s).*(vmm|vxp|3cn|3th|expansion|portable|vfs|3dmovie|3dmm|selected|temp|patch|movie|source|content).*")) chosen.add(f);
                }
                out.println();
            }
        }
        if(args.length>1) {
            for(int i=1;i<args.length;i++) {
                Address a=toAddr(args[i]);
                Function f=getFunctionContaining(a);
                if(f!=null) chosen.add(f);
            }
        }
        DecompInterface dec=new DecompInterface();
        dec.openProgram(currentProgram);
        try(PrintWriter out=new PrintWriter(new File(dir,"decompiled.c"))) {
            for(Function f:chosen) {
                if(monitor.isCancelled()) break;
                out.println("\n/* ADDRESS "+f.getEntryPoint()+" NAME "+f.getName()+" */");
                DecompileResults r=dec.decompileFunction(f,45,monitor);
                if(r.decompileCompleted()) out.println(r.getDecompiledFunction().getC());
                else out.println("/* FAILED "+r.getErrorMessage()+" */");
            }
        }
        dec.dispose();
        println("RESEARCH OUTPUT "+dir+" functions="+chosen.size());
    }
}
