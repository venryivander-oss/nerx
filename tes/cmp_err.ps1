# cmp_err.ps1 - bandingkan pesan error (kompilasi dan jalan) nrxc dengan nrxlang asli.
# Pakai (dari folder tes):  powershell -NoProfile -File cmp_err.ps1 [nama1 nama2 ...]   (tanpa nama = semua di uji_err\; -dir uji_err_id untuk errlang = id)
param([string]$list = "", [string]$dir = "uji_err")
Set-Location $PSScriptRoot
$nrxlang = "..\history\dist\nrxlang_c.exe"; $nrxc = if (Test-Path ..\src\nrxc3.exe) { "..\src\nrxc3.exe" } else { "..\src\nrxc2.exe" }
$names = if ($list) { $list -split "[ ,]+" | ? { $_ } } else { Get-ChildItem "$dir\*.nrx" | % BaseName }
$sama = 0; $beda = 0
foreach ($n in $names) {
    $asli = (cmd /c "`"$nrxlang`" run $dir\$n.nrx <NUL 2>&1" | Out-String).Trim() -replace '(nrx_\w+)\.h\b','$1'; $ka = $LASTEXITCODE
    $mini = (cmd /c "`"$nrxc`" run $dir\$n.nrx <NUL 2>&1" | Out-String).Trim() -replace ', nrx_os, nrx_db','' -replace '(nrx_\w+)\.h\b','$1'; $km = $LASTEXITCODE
    if ($asli -eq $mini -and $ka -eq $km) { $sama++ }
    else { $beda++; "BEDA $n (exit $ka vs $km)"; "   real: $asli"; "   nrxc: $mini" }
}
"sama $sama, beda $beda"
