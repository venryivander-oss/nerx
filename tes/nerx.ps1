# nerx.ps1: uji fitur khusus nerx (tanpa interpreter lama): jalankan tes\uji_nerx\*.nrx dan cocokkan dengan .out
#   powershell -NoProfile -File tes\nerx.ps1        (tambah argumen "args" tidak perlu; args dan REPL diuji di bawah)
$root = Split-Path $PSScriptRoot -Parent
$comp = "$root\src\nerx.exe"
Set-Location "$PSScriptRoot\uji_nerx"
$lulus = 0; $gagal = 0
foreach ($f in Get-ChildItem -Filter *.nrx | Sort-Object Name) {
    $out = (& $comp run $f.FullName 2>&1 | Out-String) -replace "`r", ""
    $exp = ([IO.File]::ReadAllText("$($f.DirectoryName)\$($f.BaseName).out")) -replace "`r", ""
    if ($out -eq $exp) { "SAMA   $($f.BaseName)"; $lulus++ } else { "BEDA   $($f.BaseName)"; "  harapan: $($exp -replace "`n",'|')"; "  hasil:   $($out -replace "`n",'|')"; $gagal++ }
}
# args
$a = ((& $comp run "$PSScriptRoot\uji_nerx\args\a.nrx" satu "dua tiga" 4 2>&1 | Out-String) -replace "`r", "")
if ($a -eq "3`n[satu]`n[dua tiga]`n[4]`n") { "SAMA   args"; $lulus++ } else { "BEDA   args: $($a -replace "`n",'|')"; $gagal++ }
# repl
$in = "1 + 2`nbe x = 5`nx * 2`nfunc dua(n int) int`n    return n * 2`n`ndua(21)`n:quit`n"
[IO.File]::WriteAllText("$env:TEMP\nerx_repl_uji.txt", $in)
$r = ((cmd /c "`"$comp`" repl < `"$env:TEMP\nerx_repl_uji.txt`"" | Out-String) -replace "`r", "")
if ($r -match "> 3\n" -and $r -match "> 10\n" -and $r -match "> 42\n") { "SAMA   repl"; $lulus++ } else { "BEDA   repl: $($r -replace "`n",'|')"; $gagal++ }
"lulus $lulus, gagal $gagal"