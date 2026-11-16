# nrxl (Nerxelity)

Bahasa pemrograman sederhana dengan kata kunci yang mudah dibaca. Satu file
`.nrxl` bisa langsung dijalankan, atau dijadikan `.exe` yang berdiri sendiri.

```
be nama = ask "Siapa namamu? "
print "Halo, " nama "!"

func kuadrat(n int) int
    return n * n

for i in range(5)
    print i " x " i " = " kuadrat(i)
```

## Pasang

Jalankan installer `nrxl-setup.exe`. Installer memasang `nrxlang`, gcc
untuk `nrxlang build`, dan (kalau dicentang) menambahkan `nrxlang` ke PATH,
jadi bisa dipakai dari folder mana saja.

## Pakai

```
nrxlang run program.nrxl                  jalankan langsung
nrxlang build program.nrxl                jadikan program.exe
nrxlang build program.nrxl -o game.exe
```

- Hasil `.exe` berdiri sendiri: file `.nrxl` tidak perlu ikut dibagikan.
- Hasil dan pesan error `.exe` sama persis dengan `nrxlang run`, hanya lebih
  cepat (hitungan angka bisa belasan kali lipat).
- Error tipe sudah ketahuan sebelum program jalan.

## Isi bahasa

- Variabel bertipe (`int`, `double`, `float`, `bool`, `string`, `list`, `map`)
  dan `lock` untuk nilai yang tidak boleh diubah.
- `if`, `loop`, `for ... in`, `options`, `try` / `catch`, fungsi, dan `type`
  (struct dengan method).
- File lain dengan `im`, library dengan `lib` (window, game 3D, internet,
  keyboard, suara, file, json, angka acak, konsol).
- `ffi` untuk memanggil fungsi di DLL Windows langsung, termasuk struct
  (buffer) dan callback.
- Pesan error dalam bahasa Indonesia atau Inggris (`errlang = id` / `en`).

Daftar lengkap ada di [SYNTAX.txt](SYNTAX.txt) (bahasa Inggris: [SYNTAX.en.txt](SYNTAX.en.txt)), contoh lengkap di
[contoh_fitur.nrxl](contoh_fitur.nrxl).

## Build dari kode sumber

Butuh gcc (MinGW-w64) di PATH. Dari folder ini:

```
build
```

Hasilnya di `dist\`: `nrxlang.exe`, `libnrxrt.a` (runtime untuk program hasil
build), dan `include\`. Installer dibuat dengan `installer\buat.bat`
(butuh Inno Setup 6).

## Lisensi

MIT, lihat [LICENSE](LICENSE). `lib nrx_3d.h` memakai
[raylib](https://github.com/raysan5/raylib) (lisensi zlib), gcc yang ikut
installer memakai lisensinya sendiri (folder `gcc\licenses`).
