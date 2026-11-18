# linux.ps1: bangun tes\linux\*.nrx jadi ELF (keluaran tanpa .exe), jalankan di WSL, cocokkan dengan .out (hasil Windows)
#   powershell -NoProfile -File tes\linux.ps1
$root = Split-Path $PSScriptRoot -Parent
$comp = "$root\src\nerx.exe"
Set-Location "$PSScriptRoot\linux"
$lulus = 0; $gagal = 0
foreach ($f in Get-ChildItem -Filter *.nrx | Sort-Object Name) {
    $exp = Join-Path $f.DirectoryName "$($f.BaseName).out"
    if (-not (Test-Path $exp)) { continue }
    & $comp build $f.FullName -o "$($f.DirectoryName)\$($f.BaseName)" | Out-Null
    $out = (wsl -d Ubuntu-26.04 -- bash -c "cd /mnt/c/nrxl-project/tes/linux && chmod +x $($f.BaseName) && ./$($f.BaseName) 2>&1" | Out-String) -replace "`r", ""
    $out = $out.Replace($f.DirectoryName + "\", "")
    $ex = ([IO.File]::ReadAllText($exp)) -replace "`r", "" -replace "^\uFEFF", ""
    if ($out.Trim() -eq $ex.Trim()) { "SAMA   $($f.BaseName)"; $lulus++ } else { "BEDA   $($f.BaseName)"; "  harapan: $($ex.Trim() -replace "`n",'|')"; "  hasil:   $($out.Trim() -replace "`n",'|')"; $gagal++ }
    Remove-Item "$($f.DirectoryName)\$($f.BaseName)" -ErrorAction SilentlyContinue
}
# tahap 3: nerx versi Linux (dibangun silang dari Windows, dijalankan di WSL)
Push-Location "$root\src"; & $comp build nrxc.nrx -o nerx_linux | Out-Null; Pop-Location
$o = (wsl -d Ubuntu-26.04 -- bash /mnt/c/nrxl-project/tes/linux_nerx.sh | Out-String) -replace "`r", ""
$o.TrimEnd()
if ($o -match "gagal=0") { $lulus += 9 } else { $gagal++ }
Remove-Item "$root\src\nerx_linux" -ErrorAction SilentlyContinue
"linux: lulus $lulus, gagal $gagal"
