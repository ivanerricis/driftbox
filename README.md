# Driftbox

Driftbox is a minimal audio plug-in for subtle, original "tolerance modeling". Each internal voice gets slightly different frequency response, phase, gain, saturation and noise, so several voices stop sounding identical. It does not emulate any specific hardware.

> Work in progress. The full documentation (parameters, installation, release downloads, checksum verification) arrives with the first release. See `docs/PLAN.md` for the plan and `docs/ARCHITECTURE.md` for the design.

Formats: VST3 and CLAP on Windows, macOS and Linux, plus AU on macOS.

## Build from source

Requirements: CMake >= 3.25, a C++20 compiler (Visual Studio 2026 on Windows, Xcode on macOS, GCC/Clang on Linux) and Git. Dependencies are fetched with CPM.

On Windows:

```
set CPM_SOURCE_CACHE=C:\dev\.cpm
cmake --preset win-vs
cmake --build --preset win-vs-release
ctest --preset win-vs-release
```

Other presets: `win-ninja` (from a VS developer prompt), `mac-universal`, `linux`.

## License

Driftbox is licensed under the GNU Affero General Public License v3.0 or later (see `LICENSE`). Third-party components and their licences are listed in `THIRD_PARTY_NOTICES.md`.
