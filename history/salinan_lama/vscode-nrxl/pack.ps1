# Bungkus extension jadi file .vsix untuk dipasang lokal: code --install-extension nrxl-<versi>.vsix
# Tidak mengunggah apa pun. Jalankan dari folder ini: powershell -ExecutionPolicy Bypass -File pack.ps1
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$root = $PSScriptRoot
$pkg = Get-Content (Join-Path $root "package.json") -Raw | ConvertFrom-Json
$out = Join-Path $root ("{0}-{1}.vsix" -f $pkg.name, $pkg.version)

$manifest = @"
<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011" xmlns:d="http://schemas.microsoft.com/developer/vsx-schema-design/2011">
  <Metadata>
    <Identity Language="en-US" Id="$($pkg.name)" Version="$($pkg.version)" Publisher="$($pkg.publisher)" />
    <DisplayName>$($pkg.displayName)</DisplayName>
    <Description xml:space="preserve">$($pkg.description)</Description>
    <Categories>Programming Languages,Snippets</Categories>
    <Properties>
      <Property Id="Microsoft.VisualStudio.Code.Engine" Value="$($pkg.engines.vscode)" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionDependencies" Value="" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionPack" Value="" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionKind" Value="workspace" />
    </Properties>
  </Metadata>
  <Installation>
    <InstallationTarget Id="Microsoft.VisualStudio.Code" />
  </Installation>
  <Dependencies />
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true" />
    <Asset Type="Microsoft.VisualStudio.Services.Content.Details" Path="extension/README.md" Addressable="true" />
  </Assets>
</PackageManifest>
"@

$contentTypes = @"
<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension=".json" ContentType="application/json" />
  <Default Extension=".vsixmanifest" ContentType="text/xml" />
  <Default Extension=".md" ContentType="text/markdown" />
</Types>
"@

# file yang ikut masuk ke extension (path di dalam zip pakai garis miring /)
$files = @("package.json", "language-configuration.json", "README.md",
           "syntaxes/nrxl.tmLanguage.json", "snippets/nrxl.json")

if (Test-Path $out) { Remove-Item $out }
$zip = [System.IO.Compression.ZipFile]::Open($out, "Create")
try {
    function Add-Text($name, $text) {
        $entry = $zip.CreateEntry($name)
        $w = New-Object System.IO.StreamWriter($entry.Open(), (New-Object System.Text.UTF8Encoding($false)))
        $w.Write($text)
        $w.Close()
    }
    Add-Text "extension.vsixmanifest" $manifest
    Add-Text "[Content_Types].xml" $contentTypes
    foreach ($f in $files) {
        $src = Join-Path $root ($f -replace "/", "\")
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $src, "extension/$f") | Out-Null
    }
} finally {
    $zip.Dispose()
}
Write-Output "dibuat: $out"
