# siapkan.ps1: kumpulkan isi installer di installer\paket
#   paket\nrxlang.exe, libnrxrt.a, include\     (dari dist, jalankan build dulu)
#   paket\gcc\                                   gcc ramping dari MinGW-w64 (hanya untuk C)
#   paket\SYNTAX.txt, SYNTAX.en.txt, README.md, LICENSE, contoh_fitur.nrxl
# Pemakaian: powershell -ExecutionPolicy Bypass -File installer\siapkan.ps1 [-MinGW folder]
# -MinGW: folder gcc sumber. Bawaan: installer\gcc (WinLibs gcc 16 yang sudah dirampingkan,
# lihat installer\gcc\licenses\README.txt), kalau tidak ada C:\mingw64
param([string]$MinGW = "")
$ErrorActionPreference = "Stop"
if (-not $MinGW) { $MinGW = if (Test-Path "$PSScriptRoot\gcc\bin\gcc.exe") { "$PSScriptRoot\gcc" } else { "C:\mingw64" } }
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $PSScriptRoot "paket"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory $out | Out-Null

function Salin($from, $to) {
    $dir = Split-Path $to -Parent
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
    Copy-Item -Recurse -Force $from $to
}

# nrxlang
foreach ($f in "nrxlang.exe", "libnrxrt.a", "libraylib.a", "include") { Salin "$root\dist\$f" "$out\$f" }
foreach ($f in "SYNTAX.txt", "SYNTAX.en.txt", "README.md", "LICENSE", "contoh_fitur.nrxl") { Salin "$root\$f" "$out\$f" }
Salin "$root\lib\raylib\LICENSE" "$out\licenses\raylib.txt"

# gcc: hanya yang dipakai untuk mengompilasi C (tanpa C++, Fortran, gdb, dll.)
$t = "x86_64-w64-mingw32"
$ver = (Get-ChildItem "$MinGW\libexec\gcc\$t" -Directory | Select-Object -First 1).Name
$g = "$out\gcc"
# DLL yang dibutuhkan gcc/as/ld/cc1 berbeda per versi MinGW (gcc 8: pthread saja; WinLibs
# gcc 16: juga iconv, intl, zlib, zstd, gmp, ...); yang tidak ada dilewati
$dlls = "libwinpthread-1.dll", "libgcc_s_seh-1.dll", "libiconv-2.dll", "libintl-8.dll", "zlib1.dll", "libzstd.dll"
function SalinAda($from, $to) { if (Test-Path $from) { Salin $from $to } }
foreach ($f in @("gcc.exe", "as.exe", "ld.exe") + $dlls) { SalinAda "$MinGW\bin\$f" "$g\bin\$f" }
foreach ($f in @("as.exe", "ld.exe") + $dlls) { SalinAda "$MinGW\$t\bin\$f" "$g\$t\bin\$f" }
$lx = "$MinGW\libexec\gcc\$t\$ver"
foreach ($f in "cc1.exe", "collect2.exe", "lto-wrapper.exe") { Salin "$lx\$f" "$g\libexec\gcc\$t\$ver\$f" }
# liblto_plugin(-0).dll dan DLL milik cc1 (gmp, mpfr, ...) yang ada di folder libexec
Get-ChildItem $lx -Filter *.dll | ForEach-Object { Salin $_.FullName "$g\libexec\gcc\$t\$ver\$($_.Name)" }
# cc1 dan collect2 butuh DLL ini di foldernya sendiri (gcc\bin tidak ada di PATH)
foreach ($f in $dlls) { if (-not (Test-Path "$lx\$f")) { SalinAda "$MinGW\bin\$f" "$g\libexec\gcc\$t\$ver\$f" } }
foreach ($f in "crtbegin.o", "crtend.o", "libgcc.a", "libgcc_eh.a", "include", "include-fixed") {
    Salin "$MinGW\lib\gcc\$t\$ver\$f" "$g\lib\gcc\$t\$ver\$f"
}
Salin "$MinGW\$t\include" "$g\$t\include"
# .idl (definisi COM untuk midl) tidak dipakai gcc
Get-ChildItem -Recurse "$g\$t\include" -Filter *.idl | Remove-Item -Force
# CRT dan pustaka Windows yang dipakai runtime nrxl dan link bawaan gcc; DLL lain
# (ffi, gdi32, winmm) dimuat saat jalan, jadi tidak butuh .a
New-Item -ItemType Directory -Force "$g\$t\lib" | Out-Null
$libs = "mingw32", "mingwex", "mingwthrd", "iconv", "moldname", "msvcrt", "gcc", "pthread", "winpthread", "advapi32",
        "shell32", "user32", "kernel32", "gdi32", "winmm", "winhttp", "ws2_32", "ole32",
        "oleaut32", "uuid", "comctl32", "comdlg32", "shlwapi", "opengl32", "imm32", "version"
Get-ChildItem "$MinGW\$t\lib" -File | Where-Object {
    $_.Extension -eq ".o" -or $libs -contains ($_.Name -replace '^lib|\.a$', '')
} | ForEach-Object { Copy-Item $_.FullName "$g\$t\lib\" }
# lisensi gcc dan MinGW-w64 ikut dibagikan
if (Test-Path "$MinGW\licenses") { Salin "$MinGW\licenses" "$g\licenses" }

$mb = [math]::Round(((Get-ChildItem -Recurse $out | Measure-Object Length -Sum).Sum) / 1MB, 1)
Write-Host "paket siap: $out ($mb MB, gcc $ver)"
