#!/usr/bin/env bash
# Download a pinned clap-validator, verify its SHA-256 and print the path of its executable on stdout.
# usage: get_clap_validator.sh <Windows|macOS|Linux> [dest_dir]
#
# Release 0.4.1 ships one zip per platform. The Windows zip holds clap-validator.exe directly; the Linux and
# macOS zips hold a nested .tar.gz (the macOS one has the universal binary under binaries/). Both levels are
# extracted here. To bump the version, update CLAP_VALIDATOR_VERSION, the asset patterns and the checksums.
set -euo pipefail

CLAP_VALIDATOR_VERSION="0.4.1"
os="${1:?usage: get_clap_validator.sh <Windows|macOS|Linux> [dest_dir]}"
dest="${2:-${RUNNER_TEMP:-/tmp}/clap-validator}"

# SHA-256 of the release zips (clap-validator-0.4.1-127-g152b982-*).
case "$os" in
Windows)
    pattern="*-windows.zip"; exe_name="clap-validator.exe"
    sha256="d935c3af0a45c3911ea2e900f4aa5d6709dac82bb485f0c4ce28648ab2cd0c10" ;;
macOS)
    pattern="*-macos-universal.zip"; exe_name="clap-validator"
    sha256="bbec8cd7d18274e549d5d8c12ece3cec54be966129388dd2e742b9957f2ba9f1" ;;
Linux)
    pattern="*-ubuntu-22.04.zip"; exe_name="clap-validator"
    sha256="49edadcfb407ea0dd946ce418300e853fbd2660fa4b0d00e4f19ff8eef24ad90" ;;
*) echo "unknown os: $os" >&2; exit 1 ;;
esac

sha256_of() {
    # Read from stdin: sha256sum prefixes the digest with a backslash when the file name contains one (Windows).
    if command -v sha256sum >/dev/null 2>&1; then sha256sum <"$1" | cut -d' ' -f1; else shasum -a 256 <"$1" | cut -d' ' -f1; fi
}

rm -rf "$dest"
mkdir -p "$dest"
gh release download "$CLAP_VALIDATOR_VERSION" --repo free-audio/clap-validator --pattern "$pattern" --dir "$dest" --clobber >&2

shopt -s nullglob
zips=("$dest"/$pattern)
if [[ ${#zips[@]} -ne 1 ]]; then
    echo "expected exactly one asset matching $pattern in $dest, found ${#zips[@]}" >&2
    exit 1
fi
actual="$(sha256_of "${zips[0]}")"
if [[ "$actual" != "$sha256" ]]; then
    echo "SHA-256 mismatch for ${zips[0]}: expected $sha256, got $actual" >&2
    exit 1
fi

extract="$dest/extracted"
mkdir -p "$extract"
(cd "$extract" && cmake -E tar xf "${zips[0]}") >&2
# Nested archives (Linux and macOS).
for t in "$extract"/*.tar.gz; do
    (cd "$extract" && cmake -E tar xf "$t") >&2
done

exe="$(find "$extract" -type f -name "$exe_name" | head -n1)"
if [[ -z "$exe" ]]; then
    echo "clap-validator executable not found in $extract; contents:" >&2
    find "$extract" >&2
    exit 1
fi
chmod +x "$exe" 2>/dev/null || true
echo "$exe"
