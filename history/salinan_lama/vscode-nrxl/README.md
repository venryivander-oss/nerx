# Nerxelity (nrxl) untuk VS Code

Extension ini memberi warna sintaks, cuplikan kode, dan pengaturan editor untuk file `.nrxl`.

## Isi

Kata kunci (`be`, `lock`, `func`, `if`, `options`, `im`, `ffi`, ...), teks, angka, komentar `#`, nama fungsi, baris `lib nrx_window.h`, properti `templateStyle`, jenis ffi, dan nama huruf besar seperti `MAX_HP` masing-masing mendapat warna sendiri.

Untuk cuplikan, ketik salah satu kata ini lalu tekan Tab: `func`, `if`, `ife`, `loop`, `for`, `forr`, `try`, `options`, `struct`, `lib`, `windowTemplate`, `templateStyle`, `newWindow`, atau `game`.

Di editor, `Ctrl+/` membuat komentar, tanda kurung dan kutip ditutup otomatis, dan baris baru otomatis menjorok setelah `if`, `func`, `loop`, dan blok lain.

## Pasang lokal (belum dirilis)

Dari folder `vscode-nrxl`, jalankan di PowerShell:

```
powershell -ExecutionPolicy Bypass -File pack.ps1
code --install-extension nrxl-0.1.0.vsix
```

Setelah itu restart VS Code, atau jalankan `Developer: Reload Window`.

Untuk menghapus: buka panel Extensions, cari "Nerxelity", lalu klik Uninstall.

## Merilis ke VS Code Marketplace (nanti)

1. Ganti `"publisher": "nrxl-local"` di `package.json` dengan nama publisher Marketplace-mu.
2. Kalau sudah ada, tambahkan `"repository"` dan `"icon"` di `package.json`.
3. Jalankan `npx @vscode/vsce publish`.
