#!/usr/bin/env bash
# Replaces the vendored core in native/apex-sdk-core with a revision of an
# apex-sdk-core checkout, and records it in native/apex-sdk-core/REVISION.
#
#   tools/update_core.sh ../apex-sdk-core origin/main
#   tools/update_core.sh ../apex-sdk-core v0.3.0
set -euo pipefail

if [ $# -ne 2 ]; then
  echo "usage: $0 CORE_CHECKOUT REVISION" >&2
  exit 2
fi

core_checkout=$1
core_revision=$2
repository_root=$(cd "$(dirname "$0")/.." && pwd)
vendored_core="$repository_root/native/apex-sdk-core"

# What the native build and the tests need: the library's sources and build,
# the payload spec (GraphQL documents, fixtures) and the fake platform.
core_paths=(CMakeLists.txt include src third_party spec tools/smoke/fake_apex.py THIRD_PARTY_NOTICES.md)

commit=$(git -C "$core_checkout" rev-parse --verify "$core_revision^{commit}")
rm -rf "$vendored_core"
mkdir -p "$vendored_core"
git -C "$core_checkout" archive "$commit" "${core_paths[@]}" | tar -x -C "$vendored_core"
version=$(sed -n 's/^project(apex_sdk_core VERSION \([0-9.]*\).*/\1/p' "$vendored_core/CMakeLists.txt")
printf 'apex-sdk-core %s\ncommit %s\n' "$version" "$commit" > "$vendored_core/REVISION"
echo "Vendored apex-sdk-core $version ($commit)"
