#!/usr/bin/env bash
# Download a pinned clap-validator and print the path of its executable on stdout.
# usage: get_clap_validator.sh <Windows|macOS|Linux> [dest_dir]
set -euo pipefail

CLAP_VALIDATOR_VERSION="0.4.1"
os="${1:?usage: get_clap_validator.sh <Windows|macOS|Linux> [dest_dir]}"
dest="${2:-${RUNNER_TEMP:-/tmp}/clap-validator}"

case "$os" in
Windows) pattern="*-windows.zip"; exe_name="clap-validator.exe" ;;
macOS) pattern="*-macos-universal.zip"; exe_name="clap-validator" ;;
Linux) pattern="*-ubuntu-22.04.zip"; exe_name="clap-validator" ;;
*) echo "unknown os: $os" >&2; exit 1 ;;
esac

mkdir -p "$dest"
gh release download "$CLAP_VALIDATOR_VERSION" --repo free-audio/clap-validator --pattern "$pattern" --dir "$dest" --clobber >&2
(cd "$dest" && for z in *.zip; do cmake -E tar xf "$z"; done) >&2

exe="$(find "$dest" -type f -name "$exe_name" | head -n1)"
if [[ -z "$exe" ]]; then
    echo "clap-validator executable not found in $dest" >&2
    exit 1
fi
chmod +x "$exe" 2>/dev/null || true
echo "$exe"
