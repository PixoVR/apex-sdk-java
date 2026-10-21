# apex-sdk-java

The Java SDK for the Apex platform: login, Quick ID, module access and module
lists, sessions with heartbeat, telemetry, and the hand-off back to the
launcher. It is **one JAR** with the platform client
([apex-sdk-core](https://github.com/PixoVR/apex-sdk-core)) compiled in for
every supported platform, so there is nothing else to install.

| Platform | Library inside the JAR |
| --- | --- |
| Linux x86_64 and aarch64 (glibc 2.35+) | `natives/linux-x86_64/`, `natives/linux-aarch64/libapex_java.so` |
| Windows x86_64 | `natives/windows-x86_64/apex_java.dll` |
| macOS 11+ (Apple silicon and Intel) | `natives/macos/libapex_java.dylib` |
| Android 7+ (API 24), arm64-v8a and x86_64 | `lib/arm64-v8a/`, `lib/x86_64/libapex_java.so` |

It needs Java 8 or later, or Android minSdk 24. HTTP goes through the JVM's own
`HttpURLConnection` on every platform, so the JVM's (or Android's)
certificates and proxy settings apply.

## Getting it

- **Download** `apex-sdk-java-<version>.jar` from the
  [releases](https://github.com/PixoVR/apex-sdk-java/releases) and put it on
  the classpath, or in Gradle: `implementation(files("libs/apex-sdk-java-0.1.0.jar"))`.
  The `-sources` and `-javadoc` JARs are there too.
- **Or from GitHub Packages**, as `com.pixovr:apex-sdk-java:<version>`.
  GitHub Packages needs a token with `read:packages`, even to read:

  ```kotlin
  repositories {
      maven {
          url = uri("https://maven.pkg.github.com/PixoVR/apex-sdk-java")
          credentials {
              username = providers.gradleProperty("gpr.user").get()
              password = providers.gradleProperty("gpr.key").get()
          }
      }
  }
  dependencies {
      implementation("com.pixovr:apex-sdk-java:0.1.0")
  }
  ```

## Using it

```java
import com.pixovr.apex.*;

ApexConfig config = new ApexConfig(ApexEnvironment.NA_STAGING, 42)   // this module's platform ID
    .setModuleVersion("1.4.0")
    .setDeviceId(serialNumber)
    .setPlatform("windows");

try (ApexClient client = new ApexClient(config)) {
    ApexResult login = client.login("ada", password, null).await(30_000).throwIfFailed();
    String userJson = login.getJson();                    // apex-sdk-core spec/schema/user.json

    ApexResult page = client.getModulePage("windows", ApexLifecycle.ALL, ApexModuleSort.NAME,
        0, 20, false, null).await(30_000);

    client.sessionJoin("{\"scenarioId\":\"intro\"}", null).await(30_000).throwIfFailed();
    client.sessionEvent(moduleBytes, result -> { /* runs inside poll() */ });
    // ...
    client.poll();                                       // regularly, e.g. every frame
}
```

The core's rules carry over unchanged:

- **One thread.** A client belongs to the thread that created it; call every
  method from that thread. Other threads get an `IllegalStateException`.
- **Results arrive on poll.** Every call returns an `ApexCall` at once, and
  its `ApexResult` arrives inside `poll()`. Call `poll()` regularly from the
  owner thread, or `call.await(timeoutMilliseconds)`, which polls until the
  result is in (and cancels the call on timeout).
- **Callbacks** (the `ResultCallback` passed to each call, and the
  `LogListener` and `TelemetryListener` given to the constructor) run on the
  owner thread. They may call back in, including `close()`.
- **Payloads** are the core's normalized JSON, described in apex-sdk-core's
  `spec/`. Parse them with whichever JSON library you already use.
- **Session data** is the module's own bytes (`ApexSessionFormat.DATA`), or a
  JSON object with `ApexSessionFormat.XAPI`, the format the Unity SDK 1.x sent.

### On Android

Give the client a thread of its own and poll it there:

```java
HandlerThread apexThread = new HandlerThread("Apex");
apexThread.start();
Handler apexHandler = new Handler(apexThread.getLooper());

apexHandler.post(() -> {
    client = new ApexClient(new ApexConfig(ApexEnvironment.NA_PRODUCTION, 42)
        .setDeviceId(serialNumber).setPlatform("android"));
    // Launch arguments (pixotoken, returntarget, ...) from the launcher's intent:
    String launchUrl = activity.getIntent().getDataString();
    if (launchUrl != null) {
        client.launchParseUrl(launchUrl);
    }
    client.loginWithToken(null, result -> { /* on the Apex thread */ });
    apexHandler.post(pollEveryFrame);   // a Runnable that calls client.poll() and re-posts itself after ~16 ms
});
```

The JAR's `lib/<abi>/` libraries go into the APK like any native library. If
a build leaves them out, the SDK unpacks the library from the JAR on first
use instead. The desktop libraries under `natives/` are no use in an APK;
leave them out to save about 4 MB:

```kotlin
android {
    packaging { resources { excludes += "natives/**" } }
}
```

`ApexNativeLibrary.load()` loads the library early. The system property
`apex.library.path` points it at a different file, for example a debug build
of `native/`.

## Building

Needs a JDK (17 or later to run Gradle), CMake 3.20+, a C compiler and
Python 3 for the smoke tests. The core is already in the repository, compiled
(`native/apex-sdk-core/`, see [Updating the core](#updating-the-core)), so
nothing else is needed: no apex-sdk-core checkout or token.

```sh
./gradlew build                                   # this machine's native library, the JAR, unit tests and the smoke tests
./gradlew build -PapexCoreDir=../apex-sdk-core    # the same, with the core built from an apex-sdk-core checkout
```

`./gradlew build` packs only this machine's library. CI builds every
platform and packs them all into one JAR (`.github/workflows/ci.yml`):

| Job | Does |
| --- | --- |
| `core-kit` | Checks that `native/apex-sdk-core/` holds no core source, its `SHA256SUMS`, and that its Android NDK is the one CI links with |
| `desktop` (Linux x86_64 and aarch64, Windows, macOS) | Builds the library and runs every test on that platform |
| `android` (arm64-v8a, x86_64) | NDK build (pinned to the kit's NDK, `APEX_JAVA_NDK_VERSION`); checks that only `JNI_OnLoad` is exported and that the library is 16 KB page aligned |
| `package` | Packs all six libraries into one JAR (`./gradlew jar -PprebuiltNatives`), checks it, and runs the smoke test from that JAR |
| `release` (tags `v*`) | Publishes the JARs to GitHub Packages and as a GitHub release |

### Tests

- `ApexCoreKitTest` (JUnit): the committed `Apex.h` has the ABI version the
  bridge expects (`ApexNativeBridge.EXPECTED_ABI_MAJOR` / `MINOR`).
- `ApexClientTest` (JUnit): the client offline (results on poll, callbacks,
  cancel, network errors, threads, UTF-8, closing from a callback).
- `smokeFake`, `smokeFakeXapi`: `SmokeTest` runs every call against the core's
  fake platform (`native/apex-sdk-core/testkit/tools/smoke/fake_apex.py`, from
  apex-sdk-core; needs Python 3), which checks each request; once with data sessions, once with
  xAPI.

## Layout

| Path | What it holds |
| --- | --- |
| `src/main/java/com/pixovr/apex/` | The API (`ApexClient`, `ApexConfig`, `ApexCall`, `ApexResult`, the enums) and `ApexNativeLibrary`, which finds and loads the library |
| `native/src/ApexJavaJni.c` | The JNI bridge to the core's C interface (`Apex.h`) |
| `native/CMakeLists.txt` | Builds `apex_java`: the bridge with the core linked in statically, exporting only `JNI_OnLoad` |
| `native/apex-sdk-core/` | The compiled core: `include/apex/Apex.h`, `lib/<platform>/` static libraries (`linux-x86_64`, `linux-aarch64`, `windows-x86_64`, `macos`, `android-arm64-v8a`, `android-x86_64`), the fake platform under `testkit/`, `VERSION`, `SHA256SUMS`, `THIRD_PARTY_NOTICES.md` (also packed into the JAR's `META-INF/`) |
| `tools/update_core.sh` | Replaces `native/apex-sdk-core/` with a kit from apex-sdk-core |

## Updating the core

apex-sdk-core's CI builds the kit (job `java-kit`: the static libraries with
the java.net backend for all six platforms, `Apex.h` and the smoke-test kit) and
attaches it to each release as `apex-sdk-core-java-static-<tag>.zip`. To take a
new core:

```sh
tools/update_core.sh v0.3.0          # or --run <apex-sdk-core CI run ID>, or a downloaded zip
./gradlew build
```

The script checks the kit's checksums and that it holds no source. If the new
`Apex.h` changes the ABI, update the bridge (`ApexJavaJni.c`,
`ApexNativeBridge.java`, `EXPECTED_ABI_MINOR`) and `ApexClient` to match
(`ApexCoreKitTest` fails until you do). If `androidNdk` in
`native/apex-sdk-core/VERSION` changed, set `APEX_JAVA_NDK_VERSION` in
`.github/workflows/ci.yml` to it. Commit the whole `native/apex-sdk-core/`
directory in one commit.

## Releasing

Set `version` in `build.gradle`, merge to `main`, then tag `v<version>` on
`main` and push the tag. CI checks that the tag matches the version.

## Status

Built and tested on Linux x86_64 so far; the other desktops get their first
build and test run in CI. **Not yet run on an Android device**: CI builds and
checks the Android libraries, and the core's Android code path is tested in
desktop JVMs.
