# TODO: tulis ulang library nrx_* jadi .nrxl (tanpa C, tanpa VM)

Tujuan: program yang memakai `lib nrx_xxx.h` bisa dikompilasi nrxc. Library ditulis dalam nrxl
(`selfhost\lib\nrx_xxx.nrxl`), memanggil Windows API lewat `api(idx, ...)`/blok `ffi`.
Nrxc: baris `lib nrx_xxx.h` memuat file `.nrxl` yang sesuai (sekarang baris `lib` diabaikan).

## Prasyarat di nrxc (kerjakan dulu)
- [ ] `lib nrx_xxx.h` -> muat `selfhost\lib\nrx_xxx.nrxl` seperti pustaka.nrxl
- [ ] tambah impor DLL baru (user32, winhttp, winmm, advapi32/bcrypt untuk acak) + `api()` untuk fungsi non-kernel32
- [ ] `ffi` benar-benar dipakai (sekarang diabaikan; pembungkus ada di prelude)
- [ ] tipe nilai dinamis? (fromJson/toJson butuh map/list campuran) -> putuskan: tipe `json` khusus

## Urutan library
1. [ ] **nrx_crudfile**: createFile, readFile, readLines, writeFile, writeLines, appendFile, appendLine,
       deleteFile, fileExists, listFiles, readRange, findInFile, replaceInFile
       (readFile/writeFile/fileExists sudah ada di prelude/pustaka; sisanya turunan; deleteFile = DeleteFileA,
       listFiles = FindFirstFileA/FindNextFileA)
2. [ ] **nrx_rand**: random(), random(min,max), choice(list), randomSeed(n) (LCG/xorshift sendiri, tanpa API)
3. [ ] **nrx_console**: clear, cursor, color, runCommand, commandStatus
       (escape ANSI + SetConsoleCursorPosition; runCommand = CreateProcessA + pipe)
4. [ ] **nrx_keyboard**: keyPressed, getKey (GetAsyncKeyState / ReadConsoleInput)
5. [ ] **nrx_json**: toJson, toJson(x,true), fromJson (parser rekursif murni nrxl)
6. [ ] **nrx_http**: httpGet, httpPost(+jenis,+header map), httpStatus, httpStart/httpDone/httpResult
       (WinHTTP: WinHttpOpen/Connect/OpenRequest/SendRequest/ReceiveResponse/ReadData; httpStart butuh thread -> CreateThread)
       -> setelah ini telegram_bot.nrxl bisa dikompilasi nrxc
7. [ ] **nrx_sound**: playSound, playMusic, stopMusic, stopSound (winmm: PlaySound / mciSendString)
8. [ ] **nrx_window**: paling besar. Bukan fungsi biasa: `windowTemplate`, `templateStyle`, `newWindow`, newText/newButton/newInput,
       event onKey/onMouse/onTimer, canvas (drawRect/Circle/Line/Text/Image), wait(), message loop user32/gdi32.
       Butuh callback WndProc (kode mesin) + dukungan sintaks baru di nrxc. Ini yang membuka chat_gui.nrxl dan agent.nrxl.
9. [ ] **nrx_3d**: dibungkus raylib (DLL luar) -> terakhir / mungkin dilewati

## Tes
- tiap library: program uji di `selfhost\uji\` dibandingkan dengan nrxlang asli (`uji.ps1`)
- akhir: `telegram_bot.nrxl`, `chat_gui.nrxl`, `agent.nrxl` dikompilasi nrxc dan jalan sama

## Cocok untuk Qwen (kecil dan bisa diverifikasi)
- nrx_rand, bagian turunan nrx_crudfile (readLines/appendLine/findInFile/replaceInFile) — setelah prasyarat selesai
- program uji untuk tiap library
