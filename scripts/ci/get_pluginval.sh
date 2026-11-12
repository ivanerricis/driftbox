#!/usr/bin/env bash
# Download a pinned pluginval and print the path of its executable on stdout.
# usage: get_pluginval.sh <Windows|macOS|Linux> [dest_dir]
set -euo pipefail

PLUGINVAL_VERSION="v1.0.4"
os="${1:?usage: get_pluginval.sh <Windows|macOS|Linux> [dest_dir]}"
dest="${2:-${RUNNER_TEMP:-/tmp}/pluginval}"

mkdir -p "$dest"
asset="pluginval_${os}.zip"
gh release download "$PLUGINVAL_VERSION" --repo Tracktion/pluginval --pattern "$asset" --dir "$dest" --clobber >&2
(cd "$dest" && cmake -E tar xf "$asset") >&2

case "$os" in
Windows) exe="$dest/pluginval.exe" ;;
macOS) exe="$dest/pluginval.app/Contents/MacOS/pluginval" ;;
Linux) exe="$dest/pluginval" ;;
*) echo "unknown os: $os" >&2; exit 1 ;;
esac
chmod +x "$exe" 2>/dev/null || true
echo "$exe"
