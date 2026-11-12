# Releasing

## Licence warning (read first)

Driftbox is AGPL-3.0-or-later and links JUCE under the AGPLv3 option. **Distributing binaries publicly (GitHub Release, website, forum) obliges you to make the corresponding source available** to the recipients. The public repository satisfies this. A closed-source distribution would need a commercial JUCE licence and a relicense.

## Versioning

[Semantic Versioning](https://semver.org/). Path:

1. `v0.1.0-rc1`: dry run of the whole pipeline (tag, CI build on 3 OS, zips, installers, GitHub pre-release). Delete it afterwards if not needed.
2. `v0.1.0`: first release.
3. `v0.x`: minor bumps for features, patch for fixes; breaking changes are allowed before 1.0 but must be in the changelog.

## Version bump rule

The version in `CMakeLists.txt` (`project(Driftbox VERSION x.y.z ...)`) **must equal the tag** without the `v` and without the pre-release suffix: `v0.1.0-rc1` and `v0.1.0` both need `VERSION 0.1.0`. `scripts/ci/check_version.sh` enforces it and the release workflow fails otherwise.

## Procedure

1. Move `Unreleased` entries in `CHANGELOG.md` to a new version section with the date (`git cliff` can generate them from Conventional Commits).
2. Bump `VERSION` in `CMakeLists.txt` if needed; open a PR `chore: release x.y.z` and merge it (squash).
3. Make sure CI on `main` is green, including macOS.
4. Tag the merge commit and push the tag: `git tag vX.Y.Z && git push origin vX.Y.Z`.
5. `release.yml` runs:
   - `verify`: `check_version.sh` on the tag;
   - `build`: `build.yml` on Windows, macOS and Linux with `installers: true` (tests, pluginval, auval, package, Inno Setup `.exe`, macOS `.pkg`);
   - `release`: downloads the artifacts, `scripts/create-release-artifacts.sh` fails if one is missing and writes `SHA256SUMS.txt`, git-cliff writes the notes, and `softprops/action-gh-release` creates the release. `prerelease` is true when the tag contains `-`; `make_latest` is true only for stable tags.

### Dry run without a tag

Run `gh workflow run release.yml --ref <branch> -f version=v0.1.0-rc1` (or use Actions, Run workflow). It performs every step except publishing: no tag and no GitHub release are created, and the assets are kept for 3 days as the workflow artifact `release-assets-<version>`.

## Release assets

`X.Y.Z` is the tag as pushed (`v0.1.0-rc1` gives `Driftbox-v0.1.0-rc1-...`). There is no Standalone build and no architecture suffix.

| Asset | Built by |
|---|---|
| `Driftbox-vX.Y.Z-windows.zip` (VST3, CLAP, LICENSE, README.txt) | `scripts/ci/package.sh windows` |
| `Driftbox-vX.Y.Z-macos.zip` (VST3, CLAP, AU, ad-hoc signed) | `scripts/ci/package.sh macos` |
| `Driftbox-vX.Y.Z-linux.tar.gz` (VST3, CLAP) | `scripts/ci/package.sh linux` |
| `Driftbox-vX.Y.Z-windows-setup.exe` | `scripts/ci/make_installer_windows.ps1` + `installer/windows/driftbox.iss` (Inno Setup) |
| `Driftbox-vX.Y.Z-macos.pkg` | `scripts/ci/make_installer_macos.sh` + `installer/macos/distribution.xml.in` (pkgbuild/productbuild) |
| `SHA256SUMS.txt` | `scripts/create-release-artifacts.sh` |

Installers are built from the already packaged zip, only when `build.yml` is called with `installers: true`, which only `release.yml` does. PR and main CI never build them.

### Windows installer

- Inno Setup, installed in CI with `choco install innosetup --version=6.7.3` (pinned; bump deliberately).
- Per-machine, requires admin. Components: VST3 to `%CommonProgramFiles%\VST3\Driftbox.vst3` and CLAP to `%CommonProgramFiles%\CLAP`. Shows the AGPL licence; uninstall from Apps and features removes what it installed.
- Never change the `AppId` GUID in `driftbox.iss`: it identifies the installation for upgrades and uninstall.
- Unsigned: Windows SmartScreen shows "Windows protected your PC" on first run (More info, then Run anyway).

### macOS installer

- Flat distribution `.pkg` with three selectable components (VST3, CLAP, AU, all selected by default) and a licence screen. Install is system-wide into `/Library/Audio/Plug-Ins/{VST3,CLAP,Components}`. Package ids are `com.ivanerricis.driftbox.{vst3,clap,au}`; components are non-relocatable and upgrade in place.
- The `.pkg` is unsigned and the bundles inside are ad-hoc signed. See `docs/MACOS_SIGNING.md` for Gatekeeper and the optional signing hook.
- There is no uninstaller. Delete `/Library/Audio/Plug-Ins/VST3/Driftbox.vst3`, `/Library/Audio/Plug-Ins/CLAP/Driftbox.clap` and `/Library/Audio/Plug-Ins/Components/Driftbox.component`; optionally `sudo pkgutil --forget com.ivanerricis.driftbox.<vst3|clap|au>`.

### Linux

`.tar.gz` with VST3 and CLAP; copy to `~/.vst3` and `~/.clap`. No installer.

## Local builds

- Windows (Visual Studio, Git for Windows, Inno Setup 6): `powershell -File scripts/build-release.ps1 -Version v0.1.0-rc1`. It runs configure, build, ctest, `package.sh`, the installer and writes `dist/SHA256SUMS.txt` for the Windows subset.
- macOS and Linux: `scripts/build-release.sh v0.1.0-rc1` (on macOS it also builds the `.pkg`).
- `scripts/create-release-artifacts.sh dist --version vX.Y.Z [--only windows,macos,linux]` checks the expected files and writes the checksums; `--only` is for partial local runs and is never used by CI.

## Verifying checksums

Download the assets and `SHA256SUMS.txt` into one folder. A subset is fine with `--ignore-missing`.

- Linux: `sha256sum -c SHA256SUMS.txt --ignore-missing`
- macOS: `shasum -a 256 -c SHA256SUMS.txt --ignore-missing` (or `sha256sum` from coreutils)
- Windows (PowerShell), one file: `(Get-FileHash .\Driftbox-vX.Y.Z-windows.zip -Algorithm SHA256).Hash` and compare it with the line in `SHA256SUMS.txt`. In Git Bash, `sha256sum -c SHA256SUMS.txt --ignore-missing` works as on Linux.

## Pre-release manual checklist

For both VST3 and CLAP in a DAW (for example Reaper) on each OS:

- [ ] Plugin scans and loads without errors; UI opens and closes cleanly.
- [ ] Parameter automation works; project save/reload restores all state.
- [ ] Presets load and save.
- [ ] macOS: AU validates with `auval -v aufx Drbx Ivnr` and loads in the host.
- [ ] Windows: run the setup `.exe` on a clean machine; select/deselect components, plug-ins appear in the DAW, uninstall removes them.
- [ ] macOS: install the `.pkg` (component selection, licence screen); AU, VST3 and CLAP scan in the DAW.
- [ ] `SHA256SUMS.txt` verifies on the downloaded assets.
