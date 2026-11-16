// nrx_http: ambil dan kirim data lewat HTTP/HTTPS (Windows, WinHTTP)
// dipakai di script dengan:  lib nrx_http.h
//   httpGet(url)                          isi halaman/data sebagai teks
//   httpPost(url, isi)                    kirim teks, hasilnya teks balasan
//   httpPost(url, isi, "application/json")  dengan jenis isi
//   httpPost(url, isi, jenis, header)     header tambahan: map, mis. {"Authorization": "Bearer kunci"}
//   httpGet(url, header)                  sama, untuk GET
//   httpStatus()                          kode status permintaan terakhir (200, 404, ...)
//   httpStart(url, isi, jenis, header)    seperti httpPost tapi di belakang layar: langsung kembali
//   httpDone()                            true kalau permintaan httpStart sudah selesai
//   httpResult()                          balasan httpStart (sesudah httpDone); status di httpStatus()
// Kode status selain 2xx tidak dianggap error: cek dengan httpStatus().
#ifndef NRX_HTTP_H
#define NRX_HTTP_H

#include "nrxlang.h"

int nrxHttpOwns(const char *name);
Value nrxHttpCall(const char *name, Value *args, int nargs);

#endif
