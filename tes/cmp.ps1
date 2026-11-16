# cmp.ps1: bandingkan beberapa uji saja.  powershell -NoProfile -File tes\cmp.ps1 "f_list f_map"
param([string]$list, [string]$comp = "..\src\nrxc3.exe")
Set-Location $PSScriptRoot
foreach ($n in ($list -split "[ ,]+")) {
    $f = "$PSScriptRoot\uji\$n.nrxl"
    $in = "$PSScriptRoot\uji\$n.in"; if (-not (Test-Path $in)) { $in = "NUL" }
    $asli = cmd /c "type `"$in`" | ..\history\dist\nrxlang_c.exe run `"$f`" 2>&1" | Out-String
    $ec1 = $LASTEXITCODE
    $c = & $comp $f "$PWD\cmp_$n.exe" | Select-Object -Last 1
    if ($c -notmatch "Done") { "GAGAL-KOMPILASI $n : " + ($c -replace '.*Error','Error'); continue }
    $mini = cmd /c "type `"$in`" | .\cmp_$n.exe 2>&1" | Out-String
    $ec2 = $LASTEXITCODE
    [IO.File]::Delete("$PWD\cmp_$n.exe")
    if ($asli.Trim() -eq $mini.Trim() -and $ec1 -eq $ec2) { "SAMA   $n" } else {
        "BEDA   $n (exit $ec1 vs $ec2)"
        $a = $asli.Trim() -split "`n"; $b = $mini.Trim() -split "`n"
        $max = [Math]::Max($a.Count, $b.Count); $shown = 0
        for ($i = 0; $i -lt $max -and $shown -lt 8; $i++) { if ($a[$i] -ne $b[$i]) { "   baris $($i+1): real=[$($a[$i])] nrxc=[$($b[$i])]"; $shown++ } }
    }
}