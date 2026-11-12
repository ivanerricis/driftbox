# SPDX-License-Identifier: AGPL-3.0-or-later
# Checks the Windows toolchain for building Driftbox and sets CPM_SOURCE_CACHE.
[CmdletBinding()]
param([string]$CpmCache = 'C:\dev\.cpm')

$ErrorActionPreference = 'Stop'
$missing = @()

function Test-Tool($name, $hint) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { Write-Host "[ok]      $name -> $($cmd.Source)" }
    else { Write-Host "[missing] $name ($hint)" -ForegroundColor Yellow; $script:missing += $name }
}

Test-Tool git 'winget install Git.Git'
Test-Tool cmake 'winget install Kitware.CMake (>= 3.25)'
Test-Tool ninja 'winget install Ninja-build.Ninja (optional, for win-ninja preset)'
Test-Tool clang-format 'winget install LLVM.LLVM (optional)'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vs) { Write-Host "[ok]      Visual Studio C++ tools -> $vs" }
    else { Write-Host '[missing] Visual Studio C++ workload' -ForegroundColor Yellow; $missing += 'vs' }
} else {
    Write-Host '[missing] Visual Studio (vswhere not found)' -ForegroundColor Yellow; $missing += 'vs'
}

if (-not (Test-Path $CpmCache)) { New-Item -ItemType Directory -Force $CpmCache | Out-Null }
[Environment]::SetEnvironmentVariable('CPM_SOURCE_CACHE', $CpmCache, 'User')
$env:CPM_SOURCE_CACHE = $CpmCache
Write-Host "CPM_SOURCE_CACHE set to $CpmCache (user scope; reopen terminals)."

if ($missing.Count -gt 0) { Write-Host "Missing: $($missing -join ', ')" -ForegroundColor Yellow; exit 1 }
Write-Host 'Ready. Next: cmake --preset win-vs'
