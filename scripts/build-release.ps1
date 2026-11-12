<#
.SYNOPSIS
  Local Windows release build: configure, build Release, ctest, package zip, Inno Setup installer,
  then verify the Windows artefacts and write SHA256SUMS.txt into dist/.
.EXAMPLE
  powershell -File scripts/build-release.ps1 -Version v0.1.0-rc1
.NOTES
  Needs Visual Studio (preset win-vs), Git for Windows (bash, sha256sum) and Inno Setup 6.
  Only the Windows artefacts are produced; the macOS and Linux ones come from the release workflow.
#>
param(
    [Parameter(Mandatory)][string]$Version,   # vX.Y.Z[-suffix]; the X.Y.Z part must match CMakeLists.txt
    [string]$Iscc = '',
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $root

if ($Version -notmatch '^v?\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$') { throw "Version '$Version' is not vX.Y.Z[-suffix]" }
if ($Version -notmatch '^v') { $Version = "v$Version" }

$bash = Get-Command bash -ErrorAction SilentlyContinue
$gitBash = Join-Path ${env:ProgramFiles} 'Git\bin\bash.exe'
if (Test-Path $gitBash) { $bash = $gitBash }    # prefer Git Bash over the WSL launcher in System32
elseif ($bash) { $bash = $bash.Source }
else { throw 'Git Bash not found' }

function Invoke-Step([string]$What, [scriptblock]$Block) {
    Write-Host "==> $What"
    & $Block
    if ($LASTEXITCODE -ne 0) { throw "$What failed (exit $LASTEXITCODE)" }
}

# Tag vs CMake version (a pre-release suffix is allowed).
Invoke-Step 'check version' { & $bash scripts/ci/check_version.sh $Version }
Invoke-Step 'configure (win-vs)' { cmake --preset win-vs }
Invoke-Step 'build (Release)' { cmake --build --preset win-vs-release --parallel }
if (-not $SkipTests) { Invoke-Step 'ctest' { ctest --preset win-vs-release } }

New-Item -ItemType Directory -Force dist | Out-Null
Get-ChildItem dist -Filter "Driftbox-$Version-windows*" | Remove-Item -Force
Invoke-Step 'package (zip)' { & $bash scripts/ci/package.sh windows build/win-vs dist $Version }

$zip = Join-Path $root "dist\Driftbox-$Version-windows.zip"
$installerArgs = @{ Zip = $zip; Version = $Version; OutDir = (Join-Path $root 'dist') }
if ($Iscc) { $installerArgs.Iscc = $Iscc }
Write-Host '==> installer (Inno Setup)'
& (Join-Path $PSScriptRoot 'ci\make_installer_windows.ps1') @installerArgs

Invoke-Step 'SHA256SUMS.txt' { & $bash scripts/create-release-artifacts.sh dist --version $Version --only windows }
Write-Host "Done. Artefacts in $root\dist"
Get-ChildItem dist | Where-Object { $_.Name -like "Driftbox-$Version-*" -or $_.Name -eq 'SHA256SUMS.txt' } |
    Select-Object Name, Length
