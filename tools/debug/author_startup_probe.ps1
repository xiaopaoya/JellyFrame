param([string]$Sdk, [string]$App, [string]$Output)
$ErrorActionPreference = 'Stop'
if (-not $Sdk) { $Sdk = Read-Host 'SDK directory (contains sdk-manifest.json)' }
$Sdk = $Sdk.Trim().Trim('"')
$interactiveApp = -not $App
while ($true) {
    if (-not $App) { $App = Read-Host 'Specific App folder, or jellyframe.app.json file (not parent Apps folder)' }
    $App = $App.Trim().Trim('"')
    if ((Test-Path -LiteralPath $App -PathType Leaf) -and
        (Split-Path -Leaf $App) -eq 'jellyframe.app.json') {
        $App = Split-Path -Parent $App
    }
    if ($App -and (Test-Path -LiteralPath (Join-Path $App 'jellyframe.app.json') -PathType Leaf)) { break }
    Write-Host "No jellyframe.app.json directly inside: $App"
    Write-Host 'Open the specific App folder and enter that path. No measurements have started.'
    if (-not $interactiveApp) { exit 2 }
    $App = ''
}
$python = Join-Path $Sdk 'runtime/python/python.exe'
if (-not (Test-Path -LiteralPath $python -PathType Leaf)) { throw "Missing SDK Python: $python" }
if (-not $Output) {
    $Output = Join-Path ([Environment]::GetFolderPath('MyDocuments')) ("JellyFrame-startup-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
& $python (Join-Path $PSScriptRoot 'author_startup_probe.py') --sdk $Sdk --app $App --output $Output
if ($LASTEXITCODE -ne 0) {
    $failureCode = $LASTEXITCODE
    Write-Host "Startup probe failed ($failureCode). See the error above."
    if (Test-Path -LiteralPath (Join-Path $Output 'summary.json')) {
        Write-Host "Partial measurements: $Output"
    } else {
        Write-Host 'No measurement report was generated.'
    }
    exit $failureCode
}
Write-Host "Please return the report directory: $Output"
