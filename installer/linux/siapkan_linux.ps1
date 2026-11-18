# siapkan_linux.ps1: buat website\download\nerx-<versi>-linux-x86_64.tar.gz (+ .sha256).
# nerx Linux dibangun silang dari nerx.exe (nama keluaran tanpa .exe = ELF), tar dibuat lewat WSL supaya izin file benar.
# Pemakaian: powershell -ExecutionPolicy Bypass -File installer\linux\siapkan_linux.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$ver = ([regex]::Match((Get-Content "$root\installer\nerx.iss" -Raw), '#define AppVer "([^"]+)"')).Groups[1].Value
$nama = "nerx-$ver-linux-x86_64"
$tmp = Join-Path $PSScriptRoot "paket\$nama"
if (Test-Path "$PSScriptRoot\paket") { Remove-Item -Recurse -Force "$PSScriptRoot\paket" }
New-Item -ItemType Directory -Force "$tmp\lib" | Out-Null
Push-Location "$root\src"
try {
    $o = & "$root\src\nerx.exe" build nrxc.nrx -o "$tmp\nerx" | Select-Object -Last 1
    if ($o -notmatch "Done") { throw "gagal membangun nerx Linux: $o" }
} finally { Pop-Location }
foreach ($f in "prelude.nrx", "pustaka.nrx") { Copy-Item "$root\src\$f" $tmp }
Get-ChildItem "$root\src\lib" -File -Filter *.nrx | Copy-Item -Destination "$tmp\lib"
foreach ($f in "SYNTAX.txt", "SYNTAX.en.txt", "README.md", "LICENSE") { Copy-Item "$root\$f" $tmp }
Copy-Item "$root\contoh\contoh_fitur.nrx" $tmp
Copy-Item "$PSScriptRoot\install.sh" $tmp
$dl = "$root\website\download"
New-Item -ItemType Directory -Force $dl | Out-Null
$wsl = "/mnt/" + $root.Substring(0,1).ToLower() + ($root.Substring(2) -replace '\\','/')
$sh = "set -e`ncd $wsl/installer/linux/paket`nchmod 755 $nama/nerx $nama/install.sh`ntar --owner=0 --group=0 -czf $wsl/website/download/$nama.tar.gz $nama`n"
$shf = "$PSScriptRoot\tar_tmp.sh"
[IO.File]::WriteAllText($shf, $sh.Replace("`r`n","`n"))
wsl -d Ubuntu-26.04 -- bash ($wsl + "/installer/linux/tar_tmp.sh")
Remove-Item $shf
$h = (Get-FileHash -Algorithm SHA256 "$dl\$nama.tar.gz").Hash.ToLower()
[IO.File]::WriteAllText("$dl\$nama.tar.gz.sha256", "$h  $nama.tar.gz`n", (New-Object Text.UTF8Encoding $false))
$html = Join-Path $root "website\index.html"
$t = [IO.File]::ReadAllText($html)
$n = [regex]::Replace($t, '(<code id="sha256-linux"[^>]*>)[^<]*(</code>)', "`${1}$h`${2}")
[IO.File]::WriteAllText($html, $n, (New-Object Text.UTF8Encoding $false))
Write-Host "TAR OK: website\download\$nama.tar.gz ($([math]::Round((Get-Item "$dl\$nama.tar.gz").Length/1MB,2)) MB)  SHA-256: $h"