# bench/

Pembanding kecepatan nerx dengan C (sumber untuk bagian "Close to C" di website). Cetak `done in: ... ms` sendiri.

    gcc -O2 loop300.c -o loop300_c.exe
    ..\src\nrxc.exe loop300.nrx loop300_nerx.exe
    .\loop300_c.exe
    .\loop300_nerx.exe

`loop` = 3 juta putaran, `loop300` = 300 juta putaran (`% 7`), `sum` = jumlah `i*3+1`, `fib` = fib(38) rekursif.