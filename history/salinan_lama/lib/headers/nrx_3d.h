// nrx_3d: game 3D sederhana (membungkus raylib)
// dipakai di script dengan:  lib nrx_3d.h
//   open3d(judul, lebar, tinggi)        buka window 3D
//   running3d()                         true selama window belum ditutup
//   camera3d(x, y, z, lihatX, lihatY, lihatZ)   posisi kamera dan titik yang dilihat
//   begin3d(warna)  ...  end3d()        satu frame: latar, gambar, tampilkan
//   begin3d(atas, bawah)                latar bergradasi dari atas ke bawah
//   push3d()  move3d(x,y,z)  turn3d("y", derajat)  ...  pop3d()
//                                       geser/putar semua yang digambar sampai pop3d
//   cylinder(x, y, z, jariAtas, jariBawah, tinggi, warna)   tabung/kerucut, y = dasar
//   rectGrad2d(x, y, lebar, tinggi, atas, bawah)   kotak 2D bergradasi
//   mouse3d() [x, y]   click3d() klik kiri   wheel3d() putaran roda   ground3d() [x, z] lantai di bawah mouse
//   cube(x, y, z, lebar, tinggi, dalam, warna)   sisinya diarsir; cubeWire(...) hanya garis
//   sphere(x, y, z, jari, warna)        plane(x, y, z, lebar, dalam, warna)
//   grid(jumlah, jarak)                 garis bantu di lantai
//   text2d(teks, x, y, ukuran, warna)   tulisan di layar (koordinat piksel)
//   rect2d(x, y, lebar, tinggi, warna)  kotak di layar (koordinat piksel)
//   textWidth2d(teks, ukuran)           lebar tulisan dalam piksel
//   key3d("w")                          true selama tombol ditekan
//                                       (huruf, angka, space, enter, escape, shift, up, down, left, right,
//                                        backspace, tab, ctrl)
//   pressed3d("enter")                  true sekali saat ditekan (berulang kalau ditahan)
//   typed3d()                           huruf yang diketik sejak frame lalu, untuk kotak ketik
//   clipboard3d()                       teks di clipboard (untuk Ctrl+V)
//   clipboard3d(teks)                   isi clipboard dengan teks (salin)
//   toScreen3d(x, y, z)                 posisi piksel [x, y] dari titik 3D (untuk label)
//   exitKey3d("")                       tombol penutup window (awal escape, "" = tidak ada)
//   screenshot3d("layar.png")           simpan isi window ke file .png (panggil setelah end3d)
//   fps3d(n)                            batasi frame per detik; frameTime() detik sejak frame lalu
//   close3d()                           tutup window
// warna: "red", "green", "blue", "white", "black", "gray", "darkgray", "yellow", "orange",
//        "purple", "pink", "brown", "skyblue", "#rrggbb", atau "#rrggbbaa" (aa = transparansi)
#ifndef NRX_3D_H
#define NRX_3D_H

#include <string.h>
#include "nrxlang.h"

static inline int nrx3dOwns(const char *name){
    static const char *names[]={"open3d","running3d","camera3d","begin3d","end3d","cube","cubeWire",
        "sphere","plane","grid","text2d","key3d","fps3d","frameTime","close3d",
        "pressed3d","typed3d","textWidth2d","rect2d","clipboard3d","screenshot3d","toScreen3d","exitKey3d",
        "mouse3d","click3d","wheel3d","ground3d","push3d","pop3d","move3d","turn3d","cylinder","rectGrad2d"};
    for(size_t i=0;i<sizeof names/sizeof *names;i++) if(!strcmp(name,names[i])) return 1;
    return 0;
}

// raylib hanya ikut ke program yang memakai lib nrx_3d.h: nrx3dInit() mengisi pointer ini
extern Value (*nrx3dCallPtr)(const char *name, Value *args, int nargs);
void nrx3dInit(void);

#endif
