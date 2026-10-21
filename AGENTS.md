# Working in apex-sdk-java

The Java SDK for the Apex platform: a Java API over a JNI bridge to
apex-sdk-core, packed into one JAR with the native library for every platform.
Read `README.md` for the layout, build and release.

## Rules

- The core is in this repository in compiled form only: `native/apex-sdk-core/`
  holds `Apex.h`, one static library per platform and the smoke-test kit, from
  apex-sdk-core's `java-kit` CI job. Never commit the core's sources
  (`.cpp`, `.hpp`, `src/`, `third_party/`); CI's `core-kit` job rejects them.
  Update the kit only with `tools/update_core.sh`, never by hand.
- `-PapexCoreDir` / `APEX_CORE_DIR` builds against an apex-sdk-core checkout
  instead, for testing an unreleased core; nothing else needs it.
- Java follows the usual Java conventions, with descriptive names (no single
  letters or abbreviations). It compiles to Java 8 bytecode and must run on
  Android API 24: no `java.nio.file`, `java.time` or other APIs Android 7
  lacks.
- `native/src/ApexJavaJni.c` follows the company C/C++ style of apex-sdk-core
  (its `AGENTS.md`): PascalCase functions and C struct fields, camelCase
  locals and parameters, braces on their own lines, tabs. Check it with
  `clang-format --dry-run --Werror native/src/*.c`.
- A client is single-threaded and results arrive on poll; keep both rules in
  any API you add.

## Checking

```sh
./gradlew build  # compiles with -Werror, unit tests, smoke tests
clang-format --dry-run --Werror native/src/*.c
```
