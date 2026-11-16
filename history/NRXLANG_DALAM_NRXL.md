# nrxlang versi nrxl (nrxc) - bahan review

Catatan 2026-11-16: kode C, 
rxlang.exe, dan runtime lamanya sudah dipindah ke history\ (tidak ada yang dihapus).
Nrxc dibangun dari benih selfhost\nrxc.exe; 
rxlang.exe hanya dipakai sebagai pembanding di skrip tes.

Tujuan: semua yang dikerjakan 
rxlang.exe` (C) bisa dikerjakan 
rxc.exe` (nrxl murni).
Kode C tidak diubah atau dihapus (kecuali satu blok kecil di `history\src\nrx_build.c`: 
rxlang build`
memanggil 
rxc.exe` kalau ada di sebelahnya). Bandingkan sendiri dengan skrip di bawah.

## Padanan perintah

| nrxlang (C)                        | nrxc (nrxl)                       |
|------------------------------------|-----------------------------------|
| 
rxlang run file.nrxl`            | 
rxc run file.nrxl [argumen]`    |
| 
rxlang build file.nrxl [-o x]`   | 
rxc build file.nrxl [-o x.exe]` atau 
rxc file.nrxl [x.exe]` |
| pengecekan tipe + pesan error      | di dalam nrxc, format `error E#### in file:baris: pesan` sama |


rxc run` mengompilasi ke .exe sementara di folder Temp, menjalankannya (stdin/stdout/kode keluar
diteruskan), lalu menghapusnya.

## Cara membandingkan (dari folder selfhost)

    powershell -NoProfile -File uji.ps1                 # ~80 program: nrxlang asli vs hasil nrxc (jalan)
    powershell -NoProfile -File cmp.ps1 "f_list f_map"  # beberapa program saja
    powershell -NoProfile -File cmp_err.ps1             # kasus error (uji_err\*.nrxl): pesan + kode keluar,
                                                        # nrxlang run vs nrxc run

Status terakhir: `uji.ps1` semua sama, `cmp_err.ps1` semua 105 kasus sama (103 dari 103 dengan `errlang = id`). Titik tetap: nrxc2 = nrxc3 = nrxc4 (byte sama).
Pesan error mengikuti tabel `history\src\nrx_msgs.h` (kode = (nomor baris Inggris - 5) / 2).

## Belum sama (sengaja tidak dipaksakan, perlu keputusan)

Perilaku nrxlang yang belum ditiru:
- Bilangan bulat > 64-bit melingkar (nrxlang: jadi double), `min/max` campur int/double,
  alpha parsial 3D.

Sudah ditiru (sebelumnya belum): `errlang = id` (pesan kompilasi dan jalan diterjemahkan), indentasi tidak rapi,
`func` di dalam `func`, `ask` tanpa prompt, indeks angka pada map (E0028), E0343, E0121, E0241 (di baris pemanggil), nomor baris E0240 nilai bawaan, `upper(5)` (angka jadi teks untuk fungsi teks bawaan).

Perubahan perilaku yang disengaja agar sama dengan nrxlang:
- Kondisi `if/loop/not/and/or` menerima int, double, teks, list, map (0 / kosong = false).
- `len(5)` = panjang teks angkanya, `x in 5` mencari di teks angkanya.
- Fungsi buatan pengguna selalu dikompilasi dan dicek tipenya walau tidak dipanggil
  (nrxlang juga memeriksa semuanya). Efek samping: .exe sedikit lebih besar bila ada fungsi tak terpakai.
- `readFile/writeFile/fileExists` tanpa `lib nrx_crudfile.h` = error E0066; fungsi library lain
  tanpa `lib` = E0060..E0066, E0112, E0292, E0299.
- Error `ffi`: DLL tidak ada (E0010) dan fungsi tidak ada di DLL (E0020) dicek saat kompilasi.