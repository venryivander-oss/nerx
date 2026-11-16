// nrx_window: library window untuk nrxlang
// dipakai di script dengan:  lib nrx_window.h
#ifndef NRX_WINDOW_H
#define NRX_WINDOW_H

#include "nrxlang.h"

// templateStyle nama(            gaya tampilan yang bisa dipakai newText/newButton/newInput/newWindow
//     size: 20                   ukuran huruf (piksel)
//     font: "Consolas"           jenis huruf
//     bold: true / italic: true
//     color: "biru"              warna teks (nama warna atau "#RRGGBB")
//     background: "#1e1e2e"      warna latar
//     width: 120 / height: 40    ukuran elemen (tanpa ini ukurannya otomatis)
// )
// pemakaian: newText("Halo", 20, 20, judul) atau newWindow("App", 400, 300, menu, gelap)
//
// event di windowTemplate:
//   onKey(fungsi())          fungsi(tombol)            tombol ditekan saat window aktif
//   onMouse(fungsi())        fungsi(x, y, aksi, canvas) mouse di canvas; aksi: left/right/release/move
//   onTimer(fungsi(), detik) fungsi()                  dipanggil terus setiap sekian detik

// true kalau baris ini perintah milik library window (windowTemplate, templateStyle, newWindow, closeWindow)
int nrxWindowOwns(const char *text);

// baca semua windowTemplate sebelum program jalan; enabled=0 kalau script tidak memakai lib nrx_window.h
void nrxWindowPrepass(int enabled);

// jalankan perintah window di baris pc; return baris berikutnya
int nrxWindowExec(int pc);
int nrxWindowIsTemplate(int pc);
int nrxWindowNext(int pc);

// wait() yang tetap melayani klik tombol selama menunggu
void nrxWindowWait(double sec);

// dipanggil di akhir script: tunggu sampai semua window ditutup
void nrxWindowFinish(void);

// fungsi yang dipakai di dalam ekspresi: clearCanvas, drawRect, drawCircle, drawLine, drawText, drawImage
int nrxWindowFuncOwns(const char *name);
Value nrxWindowCall(const char *name, Value *args, int nargs);

#endif
