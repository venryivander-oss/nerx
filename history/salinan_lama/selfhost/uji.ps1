# uji.ps1 - uji pembeda: jalankan tiap uji\*.nrxl dengan nrxlang ASLI dan dengan nrxc, bandingkan keluarannya.
# Jalankan dari folder selfhost:   powershell -File uji.ps1
$nrxlang = Join-Path $PSScriptRoot "..\dist\nrxlang.exe"
Set-Location $PSScriptRoot
$gagal = 0
foreach ($f in Get-ChildItem uji\*.nrxl) {
    $n = $f.BaseName
    $asli = & $nrxlang run $f.FullName 2>&1 | Out-String
    [IO.File]::WriteAllText("$PWD\j.tmp", "$($f.FullName)`n$PWD\$n.exe`n")
    $c = cmd /c "type j.tmp | `"$nrxlang`" run nrxc.nrxl" | Select-Object -Last 1
    if ($c -notmatch "Selesai") { "GAGAL  $n : kompilasi: " + ($c -replace '.*Error','Error'); $gagal++; continue }
    $mini = & ".\$n.exe" 2>&1 | Out-String
    Remove-Item "$n.exe" -ErrorAction SilentlyContinue
    if ($asli.Trim() -eq $mini.Trim()) { "SAMA   $n" } else { "BEDA   $n"; $gagal++ }
}
Remove-Item j.tmp -ErrorAction SilentlyContinue
if ($gagal -eq 0) { "Semua uji sama." } else { "$gagal uji bermasalah." ; exit 1 }
