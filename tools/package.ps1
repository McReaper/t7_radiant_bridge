# Packs a built tree into T7RadiantBridge-<version>.zip, the release's layout, which clients that ship the bridge unpack:
#   mtplugins/T7RadiantBridge/            the MTPlugins plugin, the folder to copy into <BO3 root>/bin/plugins/
#   LICENSE, NOTICE.md                    the GPL's terms travel with the binary
# Entry names are written with '/' (Compress-Archive in Windows PowerShell writes '\', which unzip mangles).
# Run from the repository root after build.bat (or CI's build).
param([Parameter(Mandatory)][string]$Version, [string]$Build = "build/Release", [string]$Out = ".")
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem

$entries = [ordered]@{
    "LICENSE"                                     = "LICENSE"
    "NOTICE.md"                                   = "NOTICE.md"
    "mtplugins/T7RadiantBridge/T7RadiantBridge.dll" = Join-Path $Build "T7RadiantBridge.dll"
    "mtplugins/T7RadiantBridge/LICENSE"           = "LICENSE"
}
$zip = [IO.Path]::GetFullPath((Join-Path $Out "T7RadiantBridge-$Version.zip"))
if (Test-Path $zip) { Remove-Item $zip }
$archive = [IO.Compression.ZipFile]::Open($zip, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($name in $entries.Keys) {
        $src = (Resolve-Path $entries[$name]).Path
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $src, $name, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
}
finally {
    $archive.Dispose()
}
Write-Output $zip
