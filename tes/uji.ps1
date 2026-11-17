# uji.ps1 - uji pembeda: jalankan tiap uji\*.nrx dengan nrxlang ASLI dan dengan nrxc, bandingkan keluarannya.
# Jalankan:   powershell -File tes\uji.ps1
$nrxlang = Join-Path $PSScriptRoot "..\history\dist\nrxlang_c.exe"
Set-Location $PSScriptRoot
$gagal = 0
foreach ($f in @(Get-ChildItem uji\*.nrx) + @(Get-ChildItem uji_im*\utama.nrx)) {
    $n = $f.BaseName; if ($n -eq "utama") { $n = "im_" + $f.Directory.Name }
    $in = Join-Path $f.DirectoryName "$n.in"; if (-not (Test-Path $in)) { $in = "NUL" }
    $asli = cmd /c "type `"$in`" | `"$nrxlang`" run `"$($f.FullName)`" 2>&1" | Out-String
    $c = & "$PSScriptRoot\..\src\nrxc3.exe" $f.FullName "$PWD\$n.exe" | Select-Object -Last 1
    if ($c -notmatch "Done") { "GAGAL  $n : kompilasi: " + ($c -replace '.*Error','Error'); $gagal++; continue }
    $mini = cmd /c "type `"$in`" | `".\$n.exe`" 2>&1" | Out-String
    Remove-Item "$n.exe" -ErrorAction SilentlyContinue
    if ($asli.Trim() -eq $mini.Trim()) { "SAMA   $n" } else { "BEDA   $n"; $gagal++ }
}
Remove-Item j.tmp -ErrorAction SilentlyContinue
if ($gagal -eq 0) { "Semua uji sama." } else { "$gagal uji bermasalah." ; exit 1 }
