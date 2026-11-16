# history/

Jalur lama nrxl yang ditulis dengan C. Tidak dipakai lagi untuk membangun atau menjalankan nrxl;
disimpan sebagai arsip dan sebagai pembanding perilaku (tes di `selfhost\` membandingkan nrxc dengan
`history\dist\nrxlang_c.exe`).

- `dist\nrxlang_c.exe`  interpreter + pengecekan tipe lama (`nrxlang run`, `nrxlang build`)
- `dist\libnrxrt.a`, `dist\libraylib.a`, `dist\include\`  runtime C untuk `nrxlang build` jalur gcc
- `src\`  kode sumber C nrxlang (`nrxlang.c`, `main.c`, `nrx_build.c`, `nrx_msgs.h`, `nrx_rt.c`)
- `lib\`  library C (`nrx_*.c`), `headers\`, dan `raylib\`
- `build.bat`  membangun ulang `dist\nrxlang_c.exe` (butuh gcc / MinGW-w64 di PATH; jalankan dari folder ini)

Penggantinya: `selfhost\nrxc.nrxl` (compiler nrxl dalam nrxl). `selfhost\nrxc.exe` adalah benih yang dipakai
untuk membangun nrxc tanpa C: `selfhost\nrxc.exe nrxc.nrxl nrxlang_baru.exe`.