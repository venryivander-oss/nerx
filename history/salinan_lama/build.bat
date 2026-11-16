@echo off
rem Build nrxlang.exe ke folder dist. Jalankan dari folder mana saja: build
rem Hasilnya:
rem   dist\nrxlang.exe      interpreter + compiler (nrxlang build file.nrxl)
rem   dist\libnrxrt.a       runtime yang dipakai program hasil nrxlang build
rem   dist\include\         header untuk kode C hasil nrxlang build
cd /d "%~dp0"
if not exist dist mkdir dist
if not exist dist\obj mkdir dist\obj
if not exist dist\include mkdir dist\include
gcc -O2 -c src/nrxlang.c -Isrc -Ilib/headers -o dist/obj/nrxlang.o || goto gagal
for %%f in (lib\*.c) do (
    gcc -O2 -c %%f -Isrc -Ilib/headers -Ilib/raylib -o dist/obj/%%~nf.o || goto gagal
)
if exist dist\libnrxrt.a del dist\libnrxrt.a
ar rcs dist/libnrxrt.a dist/obj/nrxlang.o dist/obj/nrx_window.o dist/obj/nrx_rand.o dist/obj/nrx_crudfile.o dist/obj/nrx_keyboard.o dist/obj/nrx_console.o dist/obj/nrx_sound.o dist/obj/nrx_json.o dist/obj/nrx_http.o dist/obj/nrx_3d.o || goto gagal
gcc src/main.c dist/libnrxrt.a lib/raylib/libraylib.a -lopengl32 -lgdi32 -lwinmm -o dist/nrxlang.exe || goto gagal
copy /y lib\raylib\libraylib.a dist\ >nul
copy /y src\nrxlang.h dist\include\ >nul
copy /y src\nrxrt.h dist\include\ >nul
echo BUILD OK: dist\nrxlang.exe
exit /b 0
:gagal
echo BUILD GAGAL
exit /b 1
