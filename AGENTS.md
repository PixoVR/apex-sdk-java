# Working in apex-sdk-java

The Java SDK for the Apex platform: a Java API over a JNI bridge to
apex-sdk-core, packed into one JAR with the native library for every platform.
Read `README.md` for the layout, build and release.

## Rules

- The core's sources are not in this repository and must not be added back.
  Builds take them from `-PapexCoreDir` / `APEX_CORE_DIR` (an apex-sdk-core
  checkout or source release).
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
./gradlew build -PapexCoreDir=../apex-sdk-core  # compiles with -Werror, unit tests, smoke tests
clang-format --dry-run --Werror native/src/*.c
```
