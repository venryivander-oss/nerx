// nrx_console: library tampilan console untuk nrxlang
// dipakai di script dengan:  lib nrx_console.h
//   clear()                bersihkan layar, kursor kembali ke kiri atas
//   cursor(x, y)           pindahkan kursor ke kolom x, baris y (mulai dari 0)
//   color("merah")         warna teks untuk print berikutnya
//   color("putih", "biru") warna teks + warna latar
//   color()                kembali ke warna normal
//   runCommand(perintah)   jalankan perintah shell, hasilnya output sebagai teks
//   commandStatus()        kode keluar perintah terakhir (0 = berhasil)
//   warna: hitam, merah, hijau, kuning, biru, ungu, cyan, putih, abu, atau "#RRGGBB"
#ifndef NRX_CONSOLE_H
#define NRX_CONSOLE_H

#include "nrxlang.h"

int nrxConsoleOwns(const char *name);
Value nrxConsoleCall(const char *name, Value *args, int nargs);

#endif
