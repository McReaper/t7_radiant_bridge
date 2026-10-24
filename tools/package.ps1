# Packs a built tree into T7RadiantBridge-<version>.zip, the release's layout, which clients that ship the bridge unpack:
#   T7RadiantBridge-<version>.dll         the standalone bridge a client injects, named by its version (a running
#                                         Radiant locks the one it loaded, so an update goes in beside it)
#   mtplugins/T7RadiantBridge/            the MTPlugins plugin, for a Radiant MTPlugins loads into
#   LICENSE, NOTICE.md, MinHook-LICENSE.txt   the GPL and MinHook's BSD terms travel with the binaries
# Entry names are written with '/' (Compress-Archive in Windows PowerShell writes '\', which unzip mangles).
# Run from the repository root after build.bat (or CI's build).
param([Parameter(Mandatory)][string]$Version, [string]$Build = "build/Release", [string]$Out = ".")
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem

$entries = [ordered]@{
    "T7RadiantBridge-$Version.dll"                = Join-Path $Build "T7RadiantBridgeStandalone.dll"
    "LICENSE"                                     = "LICENSE"
    "NOTICE.md"                                   = "NOTICE.md"
    "MinHook-LICENSE.txt"                         = "third_party/minhook/LICENSE.txt"
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
