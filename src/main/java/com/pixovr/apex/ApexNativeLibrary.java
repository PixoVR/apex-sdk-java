package com.pixovr.apex;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Locale;
import java.util.zip.CRC32;

/**
 * Loads the SDK's native library, apex_java, which this JAR carries for every
 * supported platform:
 *
 * <ul>
 * <li>Desktops: natives/&lt;platform&gt;/ (linux-x86_64, linux-aarch64, windows-x86_64, macos), unpacked
 * to a directory under java.io.tmpdir and loaded from there.</li>
 * <li>Android: lib/&lt;abi&gt;/libapex_java.so (arm64-v8a, x86_64), which the Android build packages into
 * the APK like any native library; if it did not, the library is unpacked from the JAR as on desktops.</li>
 * </ul>
 *
 * The system property apex.library.path names a library file to load instead.
 * ApexClient loads the library on first use; call load() to do it earlier.
 */
public final class ApexNativeLibrary {
    public static final String LIBRARY_NAME = "apex_java";
    public static final String PATH_PROPERTY = "apex.library.path";

    private static String loadedFrom;

    private ApexNativeLibrary() {
    }

    /** Loads the library once per process. Throws UnsatisfiedLinkError when this platform has none. */
    public static synchronized void load() {
        if (loadedFrom != null) {
            return;
        }

        String explicitPath = System.getProperty(PATH_PROPERTY);
        if (explicitPath != null && !explicitPath.isEmpty()) {
            String absolutePath = new File(explicitPath).getAbsolutePath();
            System.load(absolutePath);
            finishLoading(absolutePath);
            return;
        }

        String resourcePath;
        if (isAndroid()) {
            try {
                System.loadLibrary(LIBRARY_NAME);
                finishLoading("the app's native libraries");
                return;
            } catch (UnsatisfiedLinkError notPackaged) {
                resourcePath = "lib/" + getAndroidAbi() + "/" + System.mapLibraryName(LIBRARY_NAME);
            }
        } else {
            resourcePath = "natives/" + getDesktopPlatform() + "/" + System.mapLibraryName(LIBRARY_NAME);
        }

        File libraryFile = unpack(resourcePath);
        System.load(libraryFile.getAbsolutePath());
        finishLoading(libraryFile.getAbsolutePath());
    }

    /** Where the library was loaded from, or null before it is loaded. */
    public static synchronized String getLoadedFrom() {
        return loadedFrom;
    }

    /** The natives/ directory name for this desktop, e.g. "linux-x86_64". */
    public static String getDesktopPlatform() {
        String osName = System.getProperty("os.name", "").toLowerCase(Locale.ROOT);
        if (osName.startsWith("mac") || osName.startsWith("darwin")) {
            return "macos"; // one universal library for arm64 and x86_64
        }

        String operatingSystem = osName.startsWith("windows") ? "windows" : osName.startsWith("linux") ? "linux"
            : osName.replace(' ', '_');
        return operatingSystem + "-" + getCpuArchitecture();
    }

    static boolean isAndroid() {
        return System.getProperty("java.vm.name", "").toLowerCase(Locale.ROOT).contains("dalvik")
            || System.getProperty("java.vendor", "").toLowerCase(Locale.ROOT).contains("android");
    }

    private static String getCpuArchitecture() {
        String osArchitecture = System.getProperty("os.arch", "").toLowerCase(Locale.ROOT);
        if (osArchitecture.equals("amd64") || osArchitecture.equals("x86_64") || osArchitecture.equals("x64")) {
            return "x86_64";
        }

        if (osArchitecture.equals("aarch64") || osArchitecture.equals("arm64")) {
            return "aarch64";
        }

        return osArchitecture;
    }

    private static String getAndroidAbi() {
        String cpuArchitecture = getCpuArchitecture();
        if (cpuArchitecture.equals("aarch64")) {
            return "arm64-v8a";
        }

        return cpuArchitecture;
    }

    private static void finishLoading(String libraryLocation) {
        int abiVersion = ApexNativeBridge.abiVersion();
        int abiMajor = abiVersion >>> 16;
        int abiMinor = abiVersion & 0xFFFF;
        if (abiMajor != ApexNativeBridge.EXPECTED_ABI_MAJOR || abiMinor != ApexNativeBridge.EXPECTED_ABI_MINOR) {
            throw new UnsatisfiedLinkError(libraryLocation + " has core ABI " + abiMajor + "." + abiMinor
                + "; this SDK needs " + ApexNativeBridge.EXPECTED_ABI_MAJOR + "."
                + ApexNativeBridge.EXPECTED_ABI_MINOR);
        }

        loadedFrom = libraryLocation;
    }

    /**
     * Copies the library out of the JAR into java.io.tmpdir/apex-sdk-java/, named by
     * its checksum, so JVMs running the same build share one file and a different
     * build never loads a stale one.
     */
    private static File unpack(String resourcePath) {
        byte[] libraryBytes = readResource(resourcePath);
        CRC32 checksum = new CRC32();
        checksum.update(libraryBytes, 0, libraryBytes.length);
        String fileName = System.mapLibraryName(LIBRARY_NAME + "-" + Long.toHexString(checksum.getValue()));
        File unpackDirectory = new File(System.getProperty("java.io.tmpdir"), "apex-sdk-java");
        File libraryFile = new File(unpackDirectory, fileName);
        if (libraryFile.isFile() && libraryFile.length() == libraryBytes.length) {
            return libraryFile;
        }

        if (!unpackDirectory.isDirectory() && !unpackDirectory.mkdirs() && !unpackDirectory.isDirectory()) {
            throw new UnsatisfiedLinkError("cannot create " + unpackDirectory);
        }

        try {
            // Write a private copy, then move it into place, so a JVM starting at the
            // same time never loads a half-written file.
            File partialFile = File.createTempFile(LIBRARY_NAME, ".partial", unpackDirectory);
            OutputStream partialOutput = new FileOutputStream(partialFile);
            try {
                partialOutput.write(libraryBytes);
            } finally {
                partialOutput.close();
            }

            if (!partialFile.renameTo(libraryFile)) {
                // Another JVM moved its copy into place first (or the OS refuses to replace a loaded file).
                partialFile.delete();
                if (!libraryFile.isFile() || libraryFile.length() != libraryBytes.length) {
                    throw new UnsatisfiedLinkError("cannot write " + libraryFile);
                }
            }

            return libraryFile;
        } catch (IOException writeError) {
            UnsatisfiedLinkError loadError = new UnsatisfiedLinkError("cannot unpack " + resourcePath + " to "
                + unpackDirectory + ": " + writeError.getMessage());
            loadError.initCause(writeError);
            throw loadError;
        }
    }

    private static byte[] readResource(String resourcePath) {
        InputStream resourceStream = ApexNativeLibrary.class.getClassLoader() != null
            ? ApexNativeLibrary.class.getClassLoader().getResourceAsStream(resourcePath)
            : ClassLoader.getSystemResourceAsStream(resourcePath);
        if (resourceStream == null) {
            throw new UnsatisfiedLinkError("apex-sdk-java has no native library for this platform (" + resourcePath
                + "). Set -D" + PATH_PROPERTY + " to one built from native/ (see README.md).");
        }

        try {
            try {
                ByteArrayOutputStream libraryBytes = new ByteArrayOutputStream();
                byte[] readBuffer = new byte[64 * 1024];
                int readCount;
                while ((readCount = resourceStream.read(readBuffer)) != -1) {
                    libraryBytes.write(readBuffer, 0, readCount);
                }

                return libraryBytes.toByteArray();
            } finally {
                resourceStream.close();
            }
        } catch (IOException readError) {
            UnsatisfiedLinkError loadError = new UnsatisfiedLinkError("cannot read " + resourcePath);
            loadError.initCause(readError);
            throw loadError;
        }
    }
}
