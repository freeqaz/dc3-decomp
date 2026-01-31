// Import symbols from a Microsoft linker .map file
// Usage: analyzeHeadless ... -postScript ImportMapFile.java <path-to-map-file>
//@category Import

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.SymbolUtilities;
import java.io.*;
import java.util.regex.*;

public class ImportMapFile extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0) {
            printerr("Usage: ImportMapFile.java <path-to-map-file>\n");
            return;
        }

        File mapFile = new File(args[0]);
        if (!mapFile.exists()) {
            printerr("Map file not found: " + mapFile.getPath() + "\n");
            return;
        }

        printf("Importing symbols from: %s\n", mapFile.getPath());

        // Pattern: section:offset symbol address [flags] object
        // Example: 0005:00000000 ?asciiDigitToHex@@YAED@Z 82330000 f keygen_xbox.obj
        Pattern pattern = Pattern.compile(
            "\\s*[0-9a-fA-F]+:[0-9a-fA-F]+\\s+(\\S+)\\s+([0-9a-fA-F]{8})\\s+.*");

        BufferedReader reader = new BufferedReader(new FileReader(mapFile));
        String line;
        int count = 0;
        int skipped = 0;
        boolean inPublics = false;

        while ((line = reader.readLine()) != null) {
            if (line.contains("Publics by Value")) {
                inPublics = true;
                continue;
            }

            if (!inPublics) continue;

            Matcher matcher = pattern.matcher(line);
            if (matcher.matches()) {
                String symbol = matcher.group(1);
                String addrStr = matcher.group(2);

                try {
                    long addr = Long.parseLong(addrStr, 16);
                    Address address = currentProgram.getAddressFactory()
                        .getDefaultAddressSpace().getAddress(addr);

                    if (address != null && currentProgram.getMemory().contains(address)) {
                        try {
                            SymbolUtilities.createPreferredLabelOrFunctionSymbol(
                                currentProgram, address, null, symbol, SourceType.IMPORTED);
                            count++;
                            if (count <= 10) {
                                printf("  %s @ %s\n", symbol, address);
                            } else if (count == 11) {
                                printf("  ... (continuing)\n");
                            }
                        } catch (Exception e) {
                            skipped++;
                        }
                    } else {
                        skipped++;
                    }
                } catch (NumberFormatException e) {
                    skipped++;
                }
            }
        }
        reader.close();

        printf("\nImport complete: %d symbols added, %d skipped\n", count, skipped);
    }
}
