#!/usr/bin/env bash
# Build Driftbox-v<version>-macos.pkg from the macos package zip (needs macOS: ditto, pkgbuild, productbuild).
# usage: make_installer_macos.sh <package.zip> <version> <out_dir>
#   version: tag or X.Y.Z[-suffix]; a leading "v" is accepted.
# Optional signing (disabled by default, see docs/MACOS_SIGNING.md):
#   MACOS_INSTALLER_CERT      "Developer ID Installer: Name (TEAMID)" -> productbuild --sign
#   MACOS_INSTALLER_KEYCHAIN  keychain holding that identity (optional)
set -euo pipefail

zip="${1:?usage: make_installer_macos.sh <package.zip> <version> <out_dir>}"
version="${2:?missing version}"
out_dir="${3:?missing out_dir}"
version="${version#v}"
numeric="${version%%-*}"
[[ "$numeric" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "error: version '$version' is not X.Y.Z[-suffix]" >&2; exit 1; }
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
tmpl="$root/installer/macos/distribution.xml.in"
id_prefix="com.ivanerricis.driftbox"

mkdir -p "$out_dir"
out_dir="$(cd "$out_dir" && pwd)"
zip="$(cd "$(dirname "$zip")" && pwd)/$(basename "$zip")"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# ditto keeps bundle symlinks, permissions and the ad-hoc signatures intact.
mkdir "$work/src"
ditto -x -k "$zip" "$work/src"
src="$(find "$work/src" -mindepth 1 -maxdepth 1 -type d -name 'Driftbox-*' -print -quit)"
[[ -n "$src" ]] || { echo "error: no Driftbox-* folder inside $zip" >&2; exit 1; }

mkdir -p "$work/pkgs" "$work/res"
# component <id> <bundle name> <install location>
component() {
    local id="$1" bundle="$2" location="$3" croot="$work/root-$1"
    [[ -e "$src/$bundle" ]] || { echo "error: $bundle missing from $zip" >&2; exit 1; }
    mkdir -p "$croot"
    ditto "$src/$bundle" "$croot/$bundle"
    # Non-relocatable: always install to the target path, never "follow" a stray copy elsewhere.
    cat > "$work/$id.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<array>
    <dict>
        <key>RootRelativeBundlePath</key><string>${bundle}</string>
        <key>BundleIsRelocatable</key><false/>
        <key>BundleIsVersionChecked</key><false/>
        <key>BundleHasStrictIdentifier</key><false/>
        <key>BundleOverwriteAction</key><string>upgrade</string>
    </dict>
</array>
</plist>
PLIST
    pkgbuild --root "$croot" --component-plist "$work/$id.plist" \
        --identifier "$id_prefix.$id" --version "$numeric" \
        --install-location "$location" "$work/pkgs/$id.pkg"
}
component vst3       Driftbox.vst3      /Library/Audio/Plug-Ins/VST3
component clap       Driftbox.clap      /Library/Audio/Plug-Ins/CLAP
component au         Driftbox.component /Library/Audio/Plug-Ins/Components

cp "$root/LICENSE" "$work/res/license.txt"
sed "s/@VERSION@/${numeric}/g" "$tmpl" > "$work/distribution.xml"

sign_args=()
if [[ -n "${MACOS_INSTALLER_CERT:-}" ]]; then
    sign_args+=(--sign "$MACOS_INSTALLER_CERT")
    [[ -n "${MACOS_INSTALLER_KEYCHAIN:-}" ]] && sign_args+=(--keychain "$MACOS_INSTALLER_KEYCHAIN")
    echo "signing installer with: $MACOS_INSTALLER_CERT"
else
    echo "MACOS_INSTALLER_CERT not set: building an unsigned installer"
fi

name="Driftbox-v${version}-macos.pkg"
productbuild --distribution "$work/distribution.xml" --resources "$work/res" \
    --package-path "$work/pkgs" ${sign_args[@]+"${sign_args[@]}"} "$out_dir/$name"
echo "created $out_dir/$name"
