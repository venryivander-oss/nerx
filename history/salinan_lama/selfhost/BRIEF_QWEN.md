# Tugas untuk Qwen: pustaka string/list di `pustaka.nrxl`

Konteks: kita membuat compiler nrxl (`nrxc.nrxl`) yang menghasilkan .exe tanpa C. Compiler itu hanya mendukung
subset kecil bahasa nrxl, jadi fungsi bawaan nrxl asli (slice, trim, split, ...) harus DITULIS ULANG dalam
nrxl subset di file `selfhost/pustaka.nrxl`. Kamu menulis file itu. Jangan ubah file lain.

## Aturan subset (HARUS)
- Hanya: int, bool, string, `list int`, `list string`. TIDAK ada double, map, try/catch, type, lambda.
- Operasi string yang ada: `+` (gabung), `==`, `!=`, `len(s)`, `substr(s, i, n)` (n karakter dari i), `ord(s)`, `chr(n)`,
  `s[i]` TIDAK ada (pakai `substr(s, i, 1)`). List: `be xs list string`, `add(xs, x)`, `xs[i]`, `len(xs)`, `xs[i] = x` via `be xs[i] = x`.
- Lihat `prelude.nrxl` untuk fungsi yang sudah ada (strGabung, str, substr, ord, chr, indexOf, cariS, ...). Pakai itu.
- Satu perintah per baris, indentasi 4 spasi, `func nama(a tipe, b tipe) tipehasil`, `return`, `if / else if / else`, `loop kondisi`,
  `for i in range(n)`, `break`, `continue`, `be x = ...`, `be x += 1`. Tidak ada operator `?:`, `??`, `!`.
- Baris maksimum 500 karakter.

## Penamaan (overload)
- Beda tipe argumen pertama: akhiran `__tipe`, mis. `contains__string`, `contains__list_string`.
- Beda jumlah argumen: `nama_a2`, `nama_a3`, mis. `slice_a2(s, a)` dan `slice_a3(s, a, b)`.

## Daftar fungsi (perilaku harus SAMA dengan nrxl asli)
Cek perilaku asli dengan `..\dist\nrxlang.exe run coba.nrxl` (nrxlang asli punya semuanya).
1. `contains__string(s, t) bool` (sudah ada)  dan `contains__list_string(xs list string, t string) bool`
2. `trim(s string) string` (buang spasi/tab/CR/LF di kedua ujung)
3. `slice_a2(s, a)` dan `slice_a3(s, a, b)` untuk string; indeks negatif = dari belakang (slice(s,-1) = karakter terakhir); b eksklusif
4. `split(s, sep string) list string`
5. `join(xs list string, sep string) string`
6. `replace(s, a, b string) string` (ganti semua kemunculan)
7. `upper(s)`, `lower(s)` (ASCII saja)
8. `startsWith(s, t) bool`, `endsWith(s, t) bool`
9. `fileExists(nama string) bool` (pakai `readFile`/api dari prelude; kalau sulit, tulis TODO dan lewati)

## Cara menguji (WAJIB sebelum bilang selesai)
Tulis program uji di `selfhost/uji/` (mis. `pustaka_trim.nrxl`) yang memakai fungsinya dan `print` hasilnya (banyak kasus tepi:
string kosong, tanpa kecocokan, sep di ujung, indeks negatif). Lalu dari folder `selfhost`:

    powershell -NoProfile -File uji.ps1

Skrip itu menjalankan tiap program dengan nrxl ASLI dan dengan compiler kita lalu membandingkan keluarannya.
`SAMA` = lolos. `BEDA` / `GAGAL` = perbaiki sampai SAMA. Laporkan hanya setelah semua baris SAMA.
