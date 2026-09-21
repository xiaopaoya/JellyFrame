param([string]$Sdk, [string]$App, [string]$Output)
$ErrorActionPreference = 'Stop'
if (-not $Sdk) { $Sdk = Read-Host 'SDK directory (contains sdk-manifest.json)' }
if (-not $App) { $App = Read-Host 'App directory (contains jellyframe.app.json)' }
$Sdk = $Sdk.Trim('"')
$App = $App.Trim('"')
$python = Join-Path $Sdk 'runtime/python/python.exe'
if (-not (Test-Path -LiteralPath $python -PathType Leaf)) { throw "Missing SDK Python: $python" }
if (-not $Output) {
    $Output = Join-Path ([Environment]::GetFolderPath('MyDocuments')) ("JellyFrame-startup-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
& $python (Join-Path $PSScriptRoot 'author_startup_probe.py') --sdk $Sdk --app $App --output $Output
if ($LASTEXITCODE -ne 0) { throw "Startup probe failed ($LASTEXITCODE). Inspect $Output" }
Write-Host "Please return the report directory: $Output"
