#!/usr/bin/env bash
# Verify that a release tag (vX.Y.Z or vX.Y.Z-suffix) matches project(... VERSION X.Y.Z) in CMakeLists.txt.
set -euo pipefail

tag="${1:?usage: check_version.sh <tag>}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

cmake_version="$(sed -nE 's/^[[:space:]]*project\([[:space:]]*Driftbox[[:space:]]+VERSION[[:space:]]+([0-9]+(\.[0-9]+)*).*/\1/p' \
    "$root/CMakeLists.txt" | head -n1)"
if [[ -z "$cmake_version" ]]; then
    echo "error: could not find project(Driftbox VERSION ...) in CMakeLists.txt" >&2
    exit 1
fi

tag_version="${tag#refs/tags/}"
tag_version="${tag_version#v}"
tag_version="${tag_version%%-*}" # strip prerelease suffix (-rc1, -beta.2)

if [[ "$tag_version" != "$cmake_version" ]]; then
    echo "error: tag '$tag' (version $tag_version) does not match CMakeLists.txt version $cmake_version" >&2
    exit 1
fi
echo "OK: tag $tag matches CMake version $cmake_version"
