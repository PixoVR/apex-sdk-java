package com.pixovr.apex;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import org.junit.jupiter.api.Test;

/** The committed apex-sdk-core kit (native/apex-sdk-core) matches what the bridge was written for. */
class ApexCoreKitTest {
    private static final File HEADER = new File("native/apex-sdk-core/include/apex/Apex.h");

    @Test
    void headerAbiMatchesTheBridge() throws IOException {
        assertTrue(HEADER.isFile(), "missing " + HEADER);
        assertEquals(ApexNativeBridge.EXPECTED_ABI_MAJOR, headerDefine("APEX_ABI_MAJOR"), "APEX_ABI_MAJOR");
        assertEquals(ApexNativeBridge.EXPECTED_ABI_MINOR, headerDefine("APEX_ABI_MINOR"), "APEX_ABI_MINOR");
    }

    private static int headerDefine(String name) throws IOException {
        Pattern pattern = Pattern.compile("^#define " + name + " ([0-9]+)\\s*$");
        try (BufferedReader reader = new BufferedReader(
            new InputStreamReader(new FileInputStream(HEADER), StandardCharsets.UTF_8))) {
            for (String line = reader.readLine(); line != null; line = reader.readLine()) {
                Matcher matcher = pattern.matcher(line);
                if (matcher.matches()) {
                    return Integer.parseInt(matcher.group(1));
                }
            }
        }
        throw new AssertionError(name + " not found in " + HEADER);
    }
}
