#!/usr/bin/env bash
# Collect the built plugin formats into Driftbox-v<version>-<platform>.zip (Windows, macOS) or
# Driftbox-v<version>-linux.tar.gz (Linux only: a zip would lose the executable bits).
# There is no Standalone build. Installers (scripts/ci/make_installer_*, T9) are built from the zip.
# usage: package.sh <platform: windows|macos|linux> <build_dir> <out_dir> [version]
# version defaults to the one in CMakeLists.txt; a leading "v" is accepted.
set -euo pipefail

platform="${1:?usage: package.sh <windows|macos|linux> <build_dir> <out_dir> [version]}"
build_dir="${2:?missing build_dir}"
out_dir="${3:?missing out_dir}"
version="${4:-}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

case "$platform" in
windows | macos | linux) ;;
*) echo "error: unknown platform '$platform' (expected windows|macos|linux)" >&2; exit 1 ;;
esac

if [[ -z "$version" ]]; then
    version="$(sed -nE 's/^[[:space:]]*project\([[:space:]]*Driftbox[[:space:]]+VERSION[[:space:]]+([0-9]+(\.[0-9]+)*).*/\1/p' \
        "$root/CMakeLists.txt" | head -n1)"
fi
version="${version#v}"
name="Driftbox-v${version}-${platform}"

mkdir -p "$out_dir"
out_dir="$(cd "$out_dir" && pwd)"
stage="$out_dir/stage/$name"
rm -rf "$out_dir/stage"
mkdir -p "$stage"

# Locate artefacts without assuming the exact JUCE layout (<target>_artefacts[/<config>]/<format>/...).
find_one() { # $1 = name, $2 = type (f|d|any)
    if [[ "$2" == any ]]; then
        find "$build_dir" -path '*_artefacts*' -name "$1" -print -quit
    else
        find "$build_dir" -path '*_artefacts*' -type "$2" -name "$1" -print -quit
    fi
}
copy_item() { # $1 = name, $2 = type
    local src
    src="$(find_one "$1" "$2")"
    if [[ -z "$src" ]]; then echo "error: $1 not found under $build_dir" >&2; exit 1; fi
    cp -R "$src" "$stage/"
    echo "packaged $src"
}

copy_item Driftbox.vst3 d
copy_item Driftbox.clap any # a file on Windows/Linux, a bundle on macOS
if [[ "$platform" == macos ]]; then
    copy_item Driftbox.component d
fi
[[ -e "$stage/Driftbox.vst3" ]] || { echo "error: VST3 missing from package" >&2; exit 1; }
[[ -e "$stage/Driftbox.clap" ]] || { echo "error: CLAP missing from package" >&2; exit 1; }

# Ad-hoc signing on macOS (Apple signing/notarisation is a later phase).
if [[ "$platform" == macos ]]; then
    for b in "$stage"/*.vst3 "$stage"/*.component "$stage"/*.clap; do
        [[ -e "$b" ]] && codesign --force --deep --sign - "$b"
    done
fi

cp "$root/LICENSE" "$stage/LICENSE"

case "$platform" in
windows)
    install_text='Copy Driftbox.vst3 to C:\Program Files\Common Files\VST3\
Copy Driftbox.clap to C:\Program Files\Common Files\CLAP\'
    ;;
macos)
    install_text='Copy Driftbox.vst3      to ~/Library/Audio/Plug-Ins/VST3/
Copy Driftbox.component to ~/Library/Audio/Plug-Ins/Components/
Copy Driftbox.clap      to ~/Library/Audio/Plug-Ins/CLAP/

The binaries are ad-hoc signed, not notarised. If macOS refuses to load them, run:
  xattr -dr com.apple.quarantine <path to the copied item>'
    ;;
linux)
    install_text='Copy Driftbox.vst3 to ~/.vst3/
Copy Driftbox.clap to ~/.clap/'
    ;;
esac
cat > "$stage/README.txt" <<TXT
Driftbox ${version} (${platform})
Console-channel tolerance modeling: each side (L and R) gets its own channel number 1-64 with a fixed set of tiny tolerances. At Drift 0 % the tolerance chain is transparent. License: AGPL-3.0-or-later (see LICENSE).

Installation
------------
${install_text}

Rescan plugins in your host after copying.
TXT

if [[ "$platform" == linux ]]; then
    (cd "$out_dir/stage" && tar -czf "$out_dir/$name.tar.gz" -- "$name")
    echo "created $out_dir/$name.tar.gz"
else
    (cd "$out_dir/stage" && cmake -E tar cf "$out_dir/$name.zip" --format=zip -- "$name")
    echo "created $out_dir/$name.zip"
fi
rm -rf "$out_dir/stage"
