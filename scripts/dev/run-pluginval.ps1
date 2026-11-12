# SPDX-License-Identifier: AGPL-3.0-or-later
# Scarica pluginval (una volta sola, versione fissa) e valida un plugin VST3.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Vst3,
    [int]$Strictness = 10,
    [string]$ToolDir = (Join-Path $PSScriptRoot '..\..\build\tools\pluginval')
)

$ErrorActionPreference = 'Stop'
# Versione fissa (non "latest"), per avere risultati riproducibili; la CI userà la stessa.
$PluginvalVersion = 'v1.0.4'
$exe = Join-Path $ToolDir 'pluginval.exe'

if (-not (Test-Path $exe)) {
    New-Item -ItemType Directory -Force $ToolDir | Out-Null
    $zip = Join-Path $ToolDir 'pluginval.zip'
    Write-Host "Scarico pluginval $PluginvalVersion..."
    Invoke-WebRequest "https://github.com/Tracktion/pluginval/releases/download/$PluginvalVersion/pluginval_Windows.zip" -OutFile $zip
    Expand-Archive $zip -DestinationPath $ToolDir -Force
    Remove-Item $zip
}

if (-not (Test-Path $Vst3)) { throw "VST3 non trovato: $Vst3" }
& $exe --strictness-level $Strictness --validate-in-process --output-dir (Join-Path $ToolDir 'logs') $Vst3
exit $LASTEXITCODE
