# semua.ps1: bangun nrxlang dari benih (src\nrxc.exe, tanpa C), lalu jalankan semua tes; hasil ke tes\hasil_tes.txt.
# Pembanding perilaku "asli" = history\dist\nrxlang_c.exe (interpreter C lama).
# Jalankan:  powershell -NoProfile -File tes\semua.ps1
# Setelah lolos, benih bisa diganti:  copy src\nrxc3.exe src\nrxc.exe
Set-Location $PSScriptRoot
$src = Join-Path $PSScriptRoot "..\src"
$o = "hasil_tes.txt"; "mulai" | Set-Content $o
Push-Location $src
& .\nrxc.exe nrxc.nrxl nrxc2.exe | Add-Content "$PSScriptRoot\$o"
& .\nrxc2.exe nrxc.nrxl nrxc3.exe | Add-Content "$PSScriptRoot\$o"
& .\nrxc3.exe nrxc.nrxl nrxc4.exe | Add-Content "$PSScriptRoot\$o"
"fixedpoint: " + ((Get-FileHash nrxc3.exe).Hash -eq (Get-FileHash nrxc4.exe).Hash) | Add-Content "$PSScriptRoot\$o"
Pop-Location
"== uji" | Add-Content $o
(powershell -NoProfile -File uji.ps1 2>&1 | Out-String) -split "`n" | ? { $_ -notmatch '^SAMA' } | Add-Content $o
"== err" | Add-Content $o
powershell -NoProfile -File cmp_err.ps1 2>&1 | Out-String | Add-Content $o
"== err id" | Add-Content $o
powershell -NoProfile -File cmp_err.ps1 -dir uji_err_id 2>&1 | Out-String | Add-Content $o
"SELESAI" | Add-Content $o