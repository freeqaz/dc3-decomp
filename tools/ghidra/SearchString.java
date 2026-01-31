// Search for strings containing a specific pattern
// Usage: analyzeHeadless ... -postScript SearchString.java <pattern>
//@category Search

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;

public class SearchString extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0) {
            printerr("Usage: SearchString.java <pattern>\n");
            return;
        }

        String pattern = args[0];
        printf("Searching for strings containing: \"%s\"\n\n", pattern);

        // Search defined strings
        printf("=== Defined Strings ===\n");
        DataIterator dataIterator = currentProgram.getListing().getDefinedData(true);
        int count = 0;
        while (dataIterator.hasNext()) {
            Data data = dataIterator.next();
            if (data.hasStringValue()) {
                Object val = data.getValue();
                if (val != null) {
                    String str = val.toString();
                    if (str.toLowerCase().contains(pattern.toLowerCase())) {
                        printf("  %s: %s\n", data.getAddress(), str);
                        count++;
                        if (count >= 100) {
                            printf("  ... (truncated at 100)\n");
                            break;
                        }
                    }
                }
            }
        }
        printf("\nFound %d matching defined strings\n", count);

        // Raw memory search
        printf("\n=== Raw Memory Search ===\n");
        byte[] searchBytes = pattern.getBytes("US-ASCII");
        Memory mem = currentProgram.getMemory();
        Address found = mem.findBytes(mem.getMinAddress(), searchBytes, null, true, monitor);
        int rawCount = 0;
        while (found != null && rawCount < 50) {
            byte[] ctx = new byte[80];
            try {
                mem.getBytes(found, ctx);
                StringBuilder sb = new StringBuilder();
                for (int i = 0; i < ctx.length; i++) {
                    if (ctx[i] >= 0x20 && ctx[i] < 0x7f) {
                        sb.append((char)ctx[i]);
                    } else if (ctx[i] == 0) {
                        break;
                    } else {
                        sb.append('.');
                    }
                }
                printf("  %s: %s\n", found, sb.toString());
            } catch (Exception e) {}
            rawCount++;
            found = mem.findBytes(found.add(1), searchBytes, null, true, monitor);
        }
        printf("\nFound %d raw matches\n", rawCount);
    }
}
