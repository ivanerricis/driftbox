<#
.SYNOPSIS
  Build Driftbox-vX.Y.Z-windows-setup.exe from the windows package zip with Inno Setup.
.EXAMPLE
  scripts/ci/make_installer_windows.ps1 -Zip dist/Driftbox-v0.1.0-windows.zip -Version v0.1.0-rc1 -OutDir dist
#>
param(
    [Parameter(Mandatory)][string]$Zip,
    [Parameter(Mandatory)][string]$Version,   # tag or X.Y.Z[-suffix]; leading "v" accepted
    [Parameter(Mandatory)][string]$OutDir,
    [string]$Iscc = ''
)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '../..')
$versionText = $Version -replace '^v', ''
if ($versionText -notmatch '^(\d+\.\d+\.\d+)') { throw "Version '$Version' is not X.Y.Z[-suffix]" }
$numeric = $Matches[1]

if (-not $Iscc) {
    $cmd = Get-Command iscc -ErrorAction SilentlyContinue
    if ($cmd) { $Iscc = $cmd.Source }
    else {
        foreach ($p in @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe")) {
            if (Test-Path $p) { $Iscc = $p; break }
        }
    }
}
if (-not $Iscc -or -not (Test-Path $Iscc)) { throw 'ISCC.exe (Inno Setup 6) not found; pass -Iscc' }

New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$scratch = Join-Path ([IO.Path]::GetTempPath()) ("driftbox-iss-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $scratch | Out-Null
try {
    Expand-Archive -LiteralPath $Zip -DestinationPath $scratch
    $src = Get-ChildItem $scratch -Directory | Select-Object -First 1
    foreach ($f in 'Driftbox.vst3', 'Driftbox.clap', 'LICENSE') {
        if (-not (Test-Path (Join-Path $src.FullName $f))) { throw "$f missing from $Zip" }
    }
    $name = "Driftbox-v$versionText-windows-setup"
    & $Iscc "/DAppVersion=$numeric" "/DAppVersionText=$versionText" "/DSourceDir=$($src.FullName)" `
        "/DOutputDir=$OutDir" "/DOutputName=$name" (Join-Path $root 'installer/windows/driftbox.iss')
    if ($LASTEXITCODE -ne 0) { throw "iscc failed ($LASTEXITCODE)" }
    Write-Host "created $OutDir\$name.exe"
} finally {
    Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue
}
