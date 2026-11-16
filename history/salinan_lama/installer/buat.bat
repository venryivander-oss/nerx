@echo off
rem Buat installer\out\nrxl-setup.exe: build nrxlang, kumpulkan paket (dengan gcc
rem ramping dari installer\gcc, kalau tidak ada C:\mingw64), lalu Inno Setup 6.
cd /d "%~dp0"
call ..\build.bat || exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0siapkan.ps1" || exit /b 1
set iscc=
for %%d in ("%LOCALAPPDATA%\Programs\Inno Setup 6" "%ProgramFiles(x86)%\Inno Setup 6" "%ProgramFiles%\Inno Setup 6") do (
    if exist "%%~d\ISCC.exe" set iscc="%%~d\ISCC.exe"
)
if not defined iscc (
    echo Inno Setup 6 belum terpasang: https://jrsoftware.org/isdl.php
    exit /b 1
)
%iscc% /Q "%~dp0nrxl.iss" || exit /b 1
echo INSTALLER OK: installer\out\nrxl-setup.exe
