[CmdletBinding()]
param(
    [ValidateSet("Package", "Install", "Update")]
    [string]$Action = "Update",
    [string]$CodeCommand = "code",
    [string]$NpxCommand = "npx",
    [string]$PnpmCommand = "pnpm"
)

$ErrorActionPreference = "Stop"
$extensionRoot = (Resolve-Path (Join-Path $PSScriptRoot ".")).Path
$packagePath = Join-Path $extensionRoot "package.json"

if (-not (Test-Path -LiteralPath $packagePath -PathType Leaf)) {
    throw "VS Code extension manifest not found: $packagePath"
}

$manifest = Get-Content -LiteralPath $packagePath -Raw | ConvertFrom-Json
$packageName = [string]$manifest.name
$packageVersion = [string]$manifest.version
if ([string]::IsNullOrWhiteSpace($packageName) -or [string]::IsNullOrWhiteSpace($packageVersion)) {
    throw "package.json must contain non-empty name and version fields."
}

$vsixPath = Join-Path $extensionRoot ("{0}-{1}.vsix" -f $packageName, $packageVersion)

function Invoke-Tool {
    param(
        [Parameter(Mandatory = $true)]
        [string]$File,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    & $File @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $File $($Arguments -join ' ')"
    }
}

function Resolve-Vsce {
    $localVsce = Join-Path $extensionRoot "node_modules\.bin\vsce.cmd"
    if (Test-Path -LiteralPath $localVsce -PathType Leaf) {
        return [pscustomobject]@{
            File = $localVsce
            Arguments = @("package", "--no-dependencies", "--out", $vsixPath)
        }
    }

    if (-not (Get-Command $NpxCommand -ErrorAction SilentlyContinue)) {
        if (Get-Command $PnpmCommand -ErrorAction SilentlyContinue) {
            return [pscustomobject]@{
                File = $PnpmCommand
                Arguments = @("dlx", "--package", "@vscode/vsce", "vsce", "package", "--no-dependencies", "--out", $vsixPath)
            }
        }
        throw "Neither a local vsce, '$NpxCommand' or '$PnpmCommand' was found. Install Node.js/npm or run: npm install --save-dev @vscode/vsce"
    }
    return [pscustomobject]@{
        File = $NpxCommand
        Arguments = @("--yes", "@vscode/vsce", "package", "--no-dependencies", "--out", $vsixPath)
    }
}

function Package-Extension {
    $resolved = Resolve-Vsce
    Push-Location -LiteralPath $extensionRoot
    try {
        Write-Host "Packaging $packageName@$packageVersion ..."
        Invoke-Tool -File $resolved.File -Arguments $resolved.Arguments
        if (-not (Test-Path -LiteralPath $vsixPath -PathType Leaf)) {
            throw "vsce completed but did not create: $vsixPath"
        }
    } finally {
        Pop-Location
    }
    Write-Host "Created $vsixPath"
}

function Install-Extension {
    param([switch]$Force)

    if (-not (Get-Command $CodeCommand -ErrorAction SilentlyContinue)) {
        throw "VS Code command '$CodeCommand' was not found. Install the 'code' command in PATH or pass -CodeCommand with its full path."
    }
    $arguments = @("--install-extension", $vsixPath)
    if ($Force) {
        $arguments += "--force"
    }
    Write-Host "Installing $vsixPath ..."
    Invoke-Tool -File $CodeCommand -Arguments $arguments
    Write-Host "JellyFrame extension is installed. Reload VS Code windows to use the updated extension."
}

Package-Extension
switch ($Action) {
    "Package" { return }
    "Install" { Install-Extension; return }
    "Update" { Install-Extension -Force; return }
}
