# macOS signing and Gatekeeper

Driftbox releases are **not Apple-signed or notarised**. The bundles inside the zip and the `.pkg` are ad-hoc signed (`codesign --sign -`), which makes them valid on Apple Silicon but gives Gatekeeper no developer identity. The `.pkg` itself is unsigned.

## Installing the unsigned `.pkg`

A `.pkg` downloaded with a browser carries the quarantine flag, and a double-click is blocked ("cannot be opened because it is from an unidentified developer"). Either:

- right-click the `.pkg`, choose Open, then Open in the dialog; or
- try to open it once, then go to System Settings, Privacy & Security, scroll to the Security section and click "Open Anyway" next to the Driftbox message; or
- remove the flag in Terminal: `xattr -d com.apple.quarantine Driftbox-vX.Y.Z-macos.pkg`.

The installer puts VST3, CLAP and AU in `/Library/Audio/Plug-Ins/{VST3,CLAP,Components}`. Files laid down by Installer do not get the quarantine flag.

## Using the zip

Plug-ins copied from `Driftbox-vX.Y.Z-macos.zip` keep the quarantine flag when the zip was downloaded with a browser, and the host may refuse to load them. Clear it once:

```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Driftbox.vst3
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Driftbox.component
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/CLAP/Driftbox.clap
```

## Optional installer signing hook

`scripts/ci/make_installer_macos.sh` signs the `.pkg` only when `MACOS_INSTALLER_CERT` is set (for example `"Developer ID Installer: Name (TEAMID)"`), optionally with `MACOS_INSTALLER_KEYCHAIN`; it then passes `--sign` to `productbuild`. CI does not set it.

## Future: Developer ID signing and notarisation

This needs a paid Apple Developer Program account and is not set up. Outline:

1. Developer ID Application and Developer ID Installer certificates exported as `.p12`, stored as repository secrets (never committed), imported into a temporary keychain in the macOS job and deleted at the end.
2. Sign every bundle with `codesign --force --deep --options runtime --timestamp --sign "Developer ID Application: ..."` before packaging, then re-run `auval`.
3. Build the `.pkg` with `MACOS_INSTALLER_CERT` set, then `xcrun notarytool submit <pkg> --apple-id ... --team-id ... --password <app-specific password> --wait` and `xcrun stapler staple <pkg>`.
4. Verify with `codesign --verify --deep --strict -vv` and `spctl -a -vv -t install`.
