# Hitung SHA-256 installer, simpan di website\download\nerx-setup.sha256, dan isi di halaman unduh website.
# Dipanggil otomatis oleh buat.bat setelah installer jadi.
$ErrorActionPreference = "Stop"
$enc = New-Object System.Text.UTF8Encoding($false)
$exe = Join-Path $PSScriptRoot "out\nerx-setup.exe"
$site = Join-Path $PSScriptRoot "..\website"
$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $exe).Hash.ToLower()
[IO.File]::WriteAllText((Join-Path $site "download\nerx-setup.sha256"), "$hash  nerx-setup.exe`n", $enc)
$html = Join-Path $site "index.html"
$t = [IO.File]::ReadAllText($html)
$n = [regex]::Replace($t, '(<code id="sha256"[^>]*>)[^<]*(</code>)', "`${1}$hash`${2}")
if ($n -eq $t -and $t -notmatch [regex]::Escape($hash)) { throw "penanda sha256 tidak ada di index.html" }
[IO.File]::WriteAllText($html, $n, $enc)
Write-Host "SHA-256: $hash"
