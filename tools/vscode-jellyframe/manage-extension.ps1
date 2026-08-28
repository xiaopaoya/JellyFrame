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

function Assert-VsixContents {
    param([Parameter(Mandatory = $true)][string]$Path)

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $required = @(
        "extension/extension.js",
        "extension/command_diagnostics.js",
        "extension/desktop_build_setup.js",
        "extension/device_presentation.js",
        "extension/status_presentation.js",
        "extension/build_profiles.js",
        "extension/author_environment.js",
        "extension/sdk_download.js",
        "extension/sdk_archive.py",
        "extension/package.json",
        "extension/package.nls.json",
        "extension/package.nls.zh-cn.json",
        "extension/media/jellyframe.svg",
        "extension/schemas/jellyframe.app.schema.json"
    )
    $archive = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $Path))
    try {
        $entries = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($entry in $archive.Entries) {
            [void]$entries.Add($entry.FullName.Replace('\', '/'))
        }
        $missing = @($required | Where-Object { -not $entries.Contains($_) })
        if ($missing.Count -gt 0) {
            throw "VSIX is missing required files: $($missing -join ', ')"
        }
    } finally {
        $archive.Dispose()
    }
}

function Resolve-Vsce {
    # The wrappers below all invoke node. Do not select one merely because a
    # package-manager shim happens to be present on PATH.
    if (-not (Get-Command "node" -ErrorAction SilentlyContinue)) {
        return $null
    }
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
        return $null
    }
    return [pscustomobject]@{
        File = $NpxCommand
        Arguments = @("--yes", "@vscode/vsce", "package", "--no-dependencies", "--out", $vsixPath)
    }
}

function ConvertTo-XmlText {
    param([Parameter(Mandatory = $true)][string]$Value)

    return [Security.SecurityElement]::Escape($Value)
}

function Write-Utf8NoBom {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Content
    )

    [IO.File]::WriteAllText($Path, $Content, [Text.UTF8Encoding]::new($false))
}

function New-BuiltinVsix {
    $staging = Join-Path ([IO.Path]::GetTempPath()) ("jellyframe-vsix-" + [Guid]::NewGuid().ToString("N"))
    $extensionStaging = Join-Path $staging "extension"
    New-Item -ItemType Directory -Path $extensionStaging -Force | Out-Null
    try {
        $files = @(
            "extension.js",
            "command_diagnostics.js",
            "desktop_build_setup.js",
            "device_presentation.js",
            "status_presentation.js",
            "build_profiles.js",
            "author_environment.js",
            "sdk_download.js",
            "sdk_archive.py",
            "package.json",
            "package.nls.json",
            "package.nls.zh-cn.json",
            "manage-extension.ps1",
            "README.md",
            "README_zh.md"
        )
        foreach ($file in $files) {
            Copy-Item -LiteralPath (Join-Path $extensionRoot $file) -Destination (Join-Path $extensionStaging $file)
        }
        Copy-Item -LiteralPath (Join-Path $extensionRoot "LICENSE") -Destination (Join-Path $extensionStaging "LICENSE.txt")
        Copy-Item -LiteralPath (Join-Path $extensionRoot "media") -Destination (Join-Path $extensionStaging "media") -Recurse
        Copy-Item -LiteralPath (Join-Path $extensionRoot "schemas") -Destination (Join-Path $extensionStaging "schemas") -Recurse

        $id = ConvertTo-XmlText $packageName
        $version = ConvertTo-XmlText $packageVersion
        $displayName = ConvertTo-XmlText ([string]$manifest.displayName)
        $description = ConvertTo-XmlText ([string]$manifest.description)
        $repository = ConvertTo-XmlText ([string]$manifest.repository.url)
        $engine = ConvertTo-XmlText ([string]$manifest.engines.vscode)
        $publisher = ConvertTo-XmlText ([string]$manifest.publisher)
        @"
<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011">
  <Metadata>
    <Identity Language="en-US" Id="$id" Version="$version" Publisher="$publisher" />
    <DisplayName>$displayName</DisplayName>
    <Description xml:space="preserve">$description</Description>
    <Categories>Other</Categories>
    <Properties>
      <Property Id="Microsoft.VisualStudio.Code.Engine" Value="$engine" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionKind" Value="workspace" />
      <Property Id="Microsoft.VisualStudio.Code.ExecutesCode" Value="true" />
      <Property Id="Microsoft.VisualStudio.Services.Links.Source" Value="$repository" />
    </Properties>
    <License>extension/LICENSE.txt</License>
  </Metadata>
  <Installation><InstallationTarget Id="Microsoft.VisualStudio.Code" /></Installation>
  <Dependencies />
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true" />
    <Asset Type="Microsoft.VisualStudio.Services.Content.Details" Path="extension/README.md" Addressable="true" />
    <Asset Type="Microsoft.VisualStudio.Services.Content.License" Path="extension/LICENSE.txt" Addressable="true" />
  </Assets>
</PackageManifest>
"@ | ForEach-Object { Write-Utf8NoBom -Path (Join-Path $staging "extension.vsixmanifest") -Content $_ }
        @"
<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="xml" ContentType="application/xml" />
  <Default Extension="json" ContentType="application/json" />
  <Default Extension="js" ContentType="application/javascript" />
  <Default Extension="md" ContentType="text/markdown" />
  <Default Extension="ps1" ContentType="text/plain" />
  <Default Extension="svg" ContentType="image/svg+xml" />
</Types>
"@ | ForEach-Object { Write-Utf8NoBom -Path (Join-Path $staging "[Content_Types].xml") -Content $_ }
        if (Test-Path -LiteralPath $vsixPath) {
            Remove-Item -LiteralPath $vsixPath -Force
        }
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [IO.Compression.ZipFile]::CreateFromDirectory($staging, $vsixPath, [IO.Compression.CompressionLevel]::Optimal, $false)
    } finally {
        if (Test-Path -LiteralPath $staging) {
            Remove-Item -LiteralPath $staging -Recurse -Force
        }
    }
}

function Package-Extension {
    $resolved = Resolve-Vsce
    Write-Host "Packaging $packageName@$packageVersion ..."
    if ($resolved) {
        Push-Location -LiteralPath $extensionRoot
        try {
            Invoke-Tool -File $resolved.File -Arguments $resolved.Arguments
        } finally {
            Pop-Location
        }
    } else {
        Write-Host "No vsce, npx or pnpm was found; using the built-in VSIX packager."
        New-BuiltinVsix
    }
    try {
        if (-not (Test-Path -LiteralPath $vsixPath -PathType Leaf)) {
            throw "Packaging did not create: $vsixPath"
        }
        Assert-VsixContents -Path $vsixPath
    } catch {
        throw
    }
    Write-Host "Created $vsixPath"
}

function Install-Extension {
    param([switch]$Force)

    $resolvedCode = Get-Command $CodeCommand -ErrorAction SilentlyContinue
    if (-not $resolvedCode) {
        throw "VS Code command '$CodeCommand' was not found. Install the 'code' command in PATH or pass -CodeCommand with its full path."
    }
    $codeCli = $resolvedCode.Source
    if ([IO.Path]::GetFileName($codeCli).Equals("Code.exe", [StringComparison]::OrdinalIgnoreCase)) {
        $adjacentCli = Join-Path (Split-Path -Parent $codeCli) "bin\code.cmd"
        if (-not (Test-Path -LiteralPath $adjacentCli -PathType Leaf)) {
            throw "'$codeCli' is the VS Code GUI executable, not its command-line client. Use '$adjacentCli' or install the 'code' command in PATH."
        }
        Write-Host "Using the VS Code command-line client: $adjacentCli"
        $codeCli = $adjacentCli
    }
    $arguments = @("--install-extension", $vsixPath)
    if ($Force) {
        $arguments += "--force"
    }
    Write-Host "Installing $vsixPath ..."
    Invoke-Tool -File $codeCli -Arguments $arguments
    Write-Host "JellyFrame extension is installed. Reload VS Code windows to use the updated extension."
}

Package-Extension
switch ($Action) {
    "Package" { return }
    "Install" { Install-Extension; return }
    "Update" { Install-Extension -Force; return }
}
