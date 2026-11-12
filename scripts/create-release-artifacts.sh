#!/usr/bin/env bash
# Verify that every expected release artefact is present in <dir> and write <dir>/SHA256SUMS.txt.
# usage: create-release-artifacts.sh <dir> [--version vX.Y.Z] [--only windows,macos,linux]
#   --version  tag or X.Y.Z[-suffix] (leading "v" optional); default: the only Driftbox-v*-windows.zip /
#              macos.zip / linux.tar.gz found in <dir>, or the CMakeLists.txt version.
#   --only     comma-separated subset of platforms to require (default: all three). Use it for local
#              runs, e.g. --only windows on a Windows machine. The release workflow never uses it.
# The checksum file is in sha256sum format: verify with `cd <dir> && sha256sum -c SHA256SUMS.txt`.
set -euo pipefail

dir=""
version=""
only="windows,macos,linux"
while [[ $# -gt 0 ]]; do
    case "$1" in
    --version) version="${2:?--version needs a value}"; shift 2 ;;
    --only) only="${2:?--only needs a value}"; shift 2 ;;
    -h | --help) sed -n '2,9p' "$0"; exit 0 ;;
    -*) echo "error: unknown option '$1'" >&2; exit 2 ;;
    *)
        [[ -z "$dir" ]] || { echo "error: more than one directory given" >&2; exit 2; }
        dir="$1"; shift ;;
    esac
done
[[ -n "$dir" ]] || { echo "usage: create-release-artifacts.sh <dir> [--version vX.Y.Z] [--only windows,macos,linux]" >&2; exit 2; }
[[ -d "$dir" ]] || { echo "error: $dir is not a directory" >&2; exit 1; }
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ -z "$version" ]]; then
    version="$(sed -nE 's/^[[:space:]]*project\([[:space:]]*Driftbox[[:space:]]+VERSION[[:space:]]+([0-9]+(\.[0-9]+)*).*/\1/p' \
        "$root/CMakeLists.txt" | head -n1)"
fi
version="${version#v}"
[[ -n "$version" ]] || { echo "error: could not determine the version" >&2; exit 1; }
p="Driftbox-v${version}"

expected=""
IFS=',' read -r -a platforms <<<"$only"
for plat in "${platforms[@]}"; do
    case "$plat" in
    windows) expected="$expected ${p}-windows.zip ${p}-windows-setup.exe" ;;
    macos) expected="$expected ${p}-macos.zip ${p}-macos.pkg" ;;
    linux) expected="$expected ${p}-linux.tar.gz" ;;
    *) echo "error: unknown platform '$plat' in --only (windows|macos|linux)" >&2; exit 2 ;;
    esac
done

missing=0
for f in $expected; do
    if [[ ! -s "$dir/$f" ]]; then
        echo "error: missing or empty release artefact: $dir/$f" >&2
        missing=1
    fi
done
if [[ $missing -ne 0 ]]; then
    echo "found in $dir:" >&2
    ls -1 "$dir" >&2
    exit 1
fi

(
    cd "$dir"
    rm -f SHA256SUMS.txt
    # shellcheck disable=SC2086
    sha256sum -- $expected > SHA256SUMS.txt
    cat SHA256SUMS.txt
)
echo "wrote $dir/SHA256SUMS.txt ($(echo $expected | wc -w | tr -d ' ') files)"
