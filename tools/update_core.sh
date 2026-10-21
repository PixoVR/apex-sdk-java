#!/usr/bin/env bash
# Replaces native/apex-sdk-core with a compiled apex-sdk-core kit: Apex.h, the
# static libraries for every platform and the smoke-test kit, from
# apex-sdk-core's java-kit CI job (attached to its releases as
# apex-sdk-core-java-static-<tag>.zip).
#
#   tools/update_core.sh v0.3.0                   the kit attached to an apex-sdk-core release
#   tools/update_core.sh --run 123456789          the kit from an apex-sdk-core CI run (before a release)
#   tools/update_core.sh path/to/kit.zip          a kit already downloaded
#
# The first two need the GitHub CLI with read access to PixoVR/apex-sdk-core.
set -euo pipefail

CORE_REPOSITORY=PixoVR/apex-sdk-core
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TARGET="$ROOT/native/apex-sdk-core"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

case "${1:-}" in
	"" | -h | --help)
		sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
		exit 1
		;;
	--run)
		gh run download "${2:?--run needs a run ID}" --repo "$CORE_REPOSITORY" --name apex-sdk-core-java-static --dir "$WORK/download"
		;;
	*.zip)
		mkdir -p "$WORK/download"
		cp "$1" "$WORK/download/"
		;;
	*)
		gh release download "$1" --repo "$CORE_REPOSITORY" --pattern "apex-sdk-core-java-static-*.zip" --dir "$WORK/download"
		;;
esac

ZIP="$(find "$WORK/download" -name 'apex-sdk-core-java-static-*.zip' -o -name '*.zip' | head -n 1)"
test -n "$ZIP" || { echo "no kit zip found" >&2 && exit 1; }
unzip -q "$ZIP" -d "$WORK/kit"
KIT="$WORK/kit/apex-sdk-core"
test -f "$KIT/VERSION" || { echo "$ZIP is not an apex-sdk-core Java kit" >&2 && exit 1; }
(cd "$KIT" && sha256sum --check --quiet SHA256SUMS)

if find "$KIT" -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.hpp' \) | grep -q .; then
	echo "the kit contains source files" >&2 && exit 1
fi

KIT_ABI_MINOR="$(sed -nE 's/^#define APEX_ABI_MINOR ([0-9]+)/\1/p' "$KIT/include/apex/Apex.h")"
JAVA_ABI_MINOR="$(sed -nE 's/.*EXPECTED_ABI_MINOR = ([0-9]+);.*/\1/p' "$ROOT/src/main/java/com/pixovr/apex/ApexNativeBridge.java")"
if [ "$KIT_ABI_MINOR" != "$JAVA_ABI_MINOR" ]; then
	echo "warning: the kit's ABI minor is $KIT_ABI_MINOR, ApexNativeBridge.EXPECTED_ABI_MINOR is $JAVA_ABI_MINOR" >&2
fi

rm -rf "$TARGET"
cp -r "$KIT" "$TARGET"
echo "native/apex-sdk-core is now:"
cat "$TARGET/VERSION"
KIT_NDK="$(sed -n 's/^androidNdk=//p' "$TARGET/VERSION")"
CI_NDK="$(sed -nE 's/^  APEX_JAVA_NDK_VERSION: "(.*)"/\1/p' "$ROOT/.github/workflows/ci.yml")"
if [ "$KIT_NDK" != "$CI_NDK" ]; then
	echo "warning: the kit was built with NDK $KIT_NDK; set APEX_JAVA_NDK_VERSION in .github/workflows/ci.yml to match" >&2
fi
