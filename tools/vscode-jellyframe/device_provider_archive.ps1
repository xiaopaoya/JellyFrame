param(
    [Parameter(Mandatory = $true)][string]$Archive,
    [Parameter(Mandatory = $true)][string]$Destination
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = $null
try {
    $archivePath = [IO.Path]::GetFullPath($Archive)
    $destinationPath = [IO.Path]::GetFullPath($Destination)
    if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) { throw 'provider archive does not exist' }
    if (Test-Path -LiteralPath $destinationPath) {
        if (@(Get-ChildItem -LiteralPath $destinationPath -Force).Count) { throw 'provider extraction destination must be empty' }
    } else {
        [void][IO.Directory]::CreateDirectory($destinationPath)
    }
    $zip = [IO.Compression.ZipFile]::OpenRead($archivePath)
    if ($zip.Entries.Count -gt 2000) { throw 'provider archive has too many entries' }
    $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $roots = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    [long]$total = 0
    foreach ($entry in $zip.Entries) {
        $name = $entry.FullName.Replace('\', '/')
        $parts = $name.TrimEnd('/').Split('/')
        foreach ($part in $parts) {
            if (-not $part -or $part -eq '.' -or $part -eq '..' -or
                $part -match '[<>:"|?*\x00-\x1f]' -or $part -match '[. ]$' -or
                $part -match '^(?i:CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\.|$)') {
                throw "Unsafe provider archive path: $name"
            }
        }
        if (-not $names.Add($name.TrimEnd('/'))) { throw "Duplicate provider archive path: $name" }
        if ((($entry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000) { throw "Provider archive symlink: $name" }
        [void]$roots.Add($parts[0])
        $total += $entry.Length
        if ($total -gt 64MB) { throw 'provider archive exceeds extraction size limit' }
    }
    if ($roots.Count -ne 1) { throw 'provider archive must contain one root directory' }
    $rootName = @($roots)[0]
    $buffer = New-Object byte[] 65536
    foreach ($entry in $zip.Entries) {
        $target = Join-Path $destinationPath $entry.FullName.Replace('/', '\')
        if ($entry.FullName.EndsWith('/')) {
            [void][IO.Directory]::CreateDirectory($target)
            continue
        }
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))
        $source = $entry.Open()
        try {
            $output = [IO.File]::Open($target, [IO.FileMode]::CreateNew)
            try {
                [long]$written = 0
                while (($count = $source.Read($buffer, 0, $buffer.Length)) -gt 0) {
                    $written += $count
                    if ($written -gt $entry.Length) { throw 'Invalid provider entry length' }
                    $output.Write($buffer, 0, $count)
                }
                if ($written -ne $entry.Length) { throw 'Truncated provider entry' }
            } finally { $output.Dispose() }
        } finally { $source.Dispose() }
    }
    $root = Join-Path $destinationPath $rootName
    $provider = Join-Path $root 'provider\jellyframe-device.cmd'
    $manifest = @(Get-ChildItem -LiteralPath (Join-Path $root 'developer-image') -Filter '*.manifest.json' -File)
    if (-not (Test-Path -LiteralPath $provider -PathType Leaf) -or $manifest.Count -ne 1) {
        throw 'provider archive is missing exactly one provider executable or Developer Image manifest'
    }
    @{ root = $rootName; provider = 'provider\jellyframe-device.cmd'; manifest = ('developer-image\' + $manifest[0].Name) } | ConvertTo-Json -Compress
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
} finally {
    if ($zip) { $zip.Dispose() }
}
