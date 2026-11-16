// nrx_crudfile: library file (create, read, update, delete) untuk nrxlang
// dipakai di script dengan:  lib nrx_crudfile.h
//   createFile(path)          buat file kosong; false kalau file sudah ada
//   readFile(path)            seluruh isi file sebagai teks
//   readLines(path)           isi file sebagai list, satu baris per isi
//   writeFile(path, teks)     timpa isi file dengan teks
//   writeLines(path, list)    timpa isi file, satu isi list per baris
//   appendFile(path, teks)    tambahkan teks di akhir file
//   appendLine(path, teks)    tambahkan teks sebagai baris baru di akhir file
//   deleteFile(path)          hapus file; false kalau gagal
//   fileExists(path)          true kalau file ada
//   listFiles(folder)         list nama file dan folder (folder berakhiran /)
//   readRange(path, mulai, n) list n baris mulai baris ke-mulai (baris pertama = 1)
//   findInFile(path, teks)    list nomor baris yang berisi teks
//   replaceInFile(path, lama, baru)  ganti semua, hasilnya jumlah yang diganti
#ifndef NRX_CRUDFILE_H
#define NRX_CRUDFILE_H

#include "nrxlang.h"

int nrxFileOwns(const char *name);
Value nrxFileCall(const char *name, Value *args, int nargs);

#endif
