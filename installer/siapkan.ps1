# siapkan.ps1: kumpulkan isi installer di installer\paket
#   paket\nrxlang.exe                    compiler nrxl yang ditulis dengan nrxl: "nrxlang run", "nrxlang build", "nrxlang file.nrxl"
#   paket\prelude.nrxl, pustaka.nrxl     runtime yang dibaca nrxlang.exe
#   paket\lib\nrx_*.nrxl                 library (window, 3d, json, http, ...) untuk nrxc
#   paket\SYNTAX.txt, SYNTAX.en.txt, README.md, LICENSE, contoh_fitur.nrxl
# nrxlang.exe = hasil kompilasi src\nrxc.nrxl, dibangun dari benih src\nrxc.exe.
# Implementasi lama sudah dipindah ke history\.
# Pemakaian: powershell -ExecutionPolicy Bypass -File installer\siapkan.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $PSScriptRoot "paket"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory $out | Out-Null

function Salin($from, $to) {
    $dir = Split-Path $to -Parent
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
    Copy-Item -Recurse -Force $from $to
}

foreach ($f in "SYNTAX.txt", "SYNTAX.en.txt", "README.md", "LICENSE") { Salin "$root\$f" "$out\$f" }
Salin "$root\contoh\contoh_fitur.nrxl" "$out\contoh_fitur.nrxl"

# nrxlang.exe: dikompilasi dari src\nrxc.nrxl memakai benih src\nrxc.exe
$sh = Join-Path $root "src"
Push-Location $sh
try {
    $hasil = & "$sh\nrxc.exe" "nrxc.nrxl" "$out\nrxlang.exe" | Select-Object -Last 1
    if ($hasil -notmatch "Done") { throw "nrxlang gagal dikompilasi: $hasil" }
} finally { Pop-Location }
foreach ($f in "prelude.nrxl", "pustaka.nrxl") { Salin "$sh\$f" "$out\$f" }
Salin "$sh\lib" "$out\lib"
# hanya .nrxl (bukan berkas uji atau sisa lain)
Get-ChildItem "$out\lib" -File | Where-Object { $_.Extension -ne ".nrxl" } | Remove-Item -Force

$mb = [math]::Round(((Get-ChildItem -Recurse $out | Measure-Object Length -Sum).Sum) / 1MB, 1)
Write-Host "paket siap: $out ($mb MB)"