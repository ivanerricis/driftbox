#!/usr/bin/env bash
# Local release build for macOS and Linux: configure, build, ctest, package (+ .pkg on macOS) and
# SHA256SUMS.txt for this platform's artefacts in dist/.
# usage: scripts/build-release.sh vX.Y.Z[-suffix]
# Windows has its own wrapper: scripts/build-release.ps1. The full set of artefacts comes from release.yml.
set -euo pipefail

version="${1:?usage: build-release.sh vX.Y.Z[-suffix]}"
[[ "$version" == v* ]] || version="v$version"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

case "$(uname -s)" in
Darwin) preset=mac-universal; platform=macos ;;
Linux) preset=linux; platform=linux ;;
*) echo "error: unsupported OS $(uname -s); on Windows use scripts/build-release.ps1" >&2; exit 1 ;;
esac

scripts/ci/check_version.sh "$version"
cmake --preset "$preset"
cmake --build --preset "$preset" --parallel
if [[ "$platform" == linux && -z "${DISPLAY:-}" ]] && command -v xvfb-run >/dev/null 2>&1; then
    xvfb-run -a ctest --preset "$preset"
else
    ctest --preset "$preset"
fi

mkdir -p dist
rm -f "dist/Driftbox-${version}-${platform}"*
scripts/ci/package.sh "$platform" "build/$preset" dist "$version"
if [[ "$platform" == macos ]]; then
    scripts/ci/make_installer_macos.sh "dist/Driftbox-${version}-macos.zip" "$version" dist
fi
scripts/create-release-artifacts.sh dist --version "$version" --only "$platform"
echo "Done. Artefacts in $root/dist"
