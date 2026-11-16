# TODO untuk Qwen (bahasa: nrxl, BUKAN C)

Konteks: `nrxc.nrxl` = compiler nrxl yang menghasilkan .exe tanpa C. Tugasmu: **menulis program uji** supaya compiler itu ketahuan kalau salah.
Jangan ubah `nrxc.nrxl`, `prelude.nrxl`, `pustaka.nrxl`, `uji.ps1`. Kamu hanya MENAMBAH file baru di folder `selfhost\uji\`.
Baca `..\SYNTAX.txt` untuk sintaks. Semua program ditulis dalam `.nrxl`.

## Batas bahasa (subset yang didukung nrxc)
- Tipe: `int`, `bool`, `string`, `list int`, `list string`, `type` sederhana (field tanpa nilai bawaan).
- Boleh: `be`, `+=`, `-=`, `print a b "c"`, `if / else if / else`, `loop kondisi`, `for i in range(n)`, `for x in list`, `break`, `continue`,
  `func` dengan tipe parameter, `return`, `lock`, `delete x`.
- JANGAN: double/float, `/` (pakai `%` dan `*`), map, try/catch, `options`, `own`, `init`, `lib`, `ffi`, tab untuk indentasi (pakai 4 spasi).
- Fungsi bawaan yang boleh: len, add, substr, str, int, split, join, replace, trim, slice, upper, lower, reverse, contains, indexOf, ord(s, i), chr(n).

## Cara kerja per file
1. Tulis `uji\nama.nrxl` (cetak banyak hasil lewat `print`, termasuk kasus tepi).
2. Jalankan dari folder `selfhost`:  `powershell -NoProfile -File uji.ps1`
3. Baris `SAMA nama` = lolos. `BEDA` atau `GAGAL` = JANGAN diperbaiki sendiri; catat di `temuan.md`: nama file, baris yang dicurigai, keluaran asli vs keluaran compiler.
4. Satu file = satu topik, 20-60 baris. Beri nama jelas.

## Daftar file uji yang harus dibuat
- [ ] `rekursi.nrxl` - faktorial 1..15, fibonacci rekursif sampai 20, gcd, pangkat
- [ ] `string_ops.nrxl` - semua fungsi string (trim, slice, split, join, replace, upper, lower, reverse, contains) dengan kasus tepi: string kosong, tanpa kecocokan, indeks negatif
- [ ] `list_ops.nrxl` - add, indeks, len, for atas list, list kosong, list string, list di dalam fungsi (dikirim sebagai argumen dan dikembalikan)
- [ ] `sortir.nrxl` - bubble sort, insertion sort, selection sort untuk list int (data 10-20 angka, termasuk duplikat)
- [ ] `prima.nrxl` - saringan prima sampai 200, hitung jumlahnya
- [ ] `loop_bersarang.nrxl` - loop dalam loop dengan break/continue di tingkat berbeda, tabel perkalian
- [ ] `tipe_struct.nrxl` - `type Titik` dengan field int dan string, list berisi Titik, ubah field lewat fungsi (referensi dibagi)
- [ ] `global_local.nrxl` - variabel global dibaca/diubah dari fungsi, variabel lokal dengan nama sama, `lock`
- [ ] `bool_logika.nrxl` - and, or, not, perbandingan, hasil bool dicetak (`true`/`false`), fungsi yang mengembalikan bool
- [ ] `kalimat.nrxl` - hitung kata, balik kata, huruf besar awal tiap kata, palindrom
- [ ] `caesar.nrxl` - sandi caesar dengan ord/chr (hanya huruf a-z), enkripsi lalu dekripsi
- [ ] `biner.nrxl` - ubah int ke string biner dan sebaliknya memakai `%` dan perkalian
- [ ] `hapus_memori.nrxl` - buat list/string besar di loop lalu `delete` tiap putaran (tidak boleh crash)
- [ ] `indentasi_dalam.nrxl` - if/else bersarang 6 tingkat dan fungsi yang memanggil fungsi lain 5 tingkat

## Selesai kalau
Semua file di atas ada, `uji.ps1` dijalankan terakhir kali, dan `temuan.md` berisi daftar BEDA/GAGAL (atau tulis "tidak ada").
