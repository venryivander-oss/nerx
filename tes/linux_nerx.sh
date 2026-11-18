# dijalankan di WSL oleh linux.ps1: uji nerx versi Linux (tahap 3)
cd /mnt/c/nrxl-project/src || exit 1
chmod +x nerx_linux
T=$(mktemp -d)
gagal=0
cek() { if [ "$2" = "$3" ]; then echo "SAMA   $1"; else echo "BEDA   $1: [$2] vs [$3]"; gagal=$((gagal+1)); fi; }
printf 'print "halo " + str(1 + 2)\n' > $T/h.nrx
cek nerx_run "$(./nerx_linux run $T/h.nrx)" "halo 3"
./nerx_linux build $T/h.nrx -o $T/h > /dev/null
cek nerx_build "$($T/h)" "halo 3"
./nerx_linux build nrxc.nrx -o $T/self > /dev/null
cek nerx_selfhost "$(sha256sum < $T/self)" "$(sha256sum < nerx_linux)"
printf 'assert(1 + 1 == 2, "x")\n' > $T/test_ok.nrx
printf 'assert(false)\n' > $T/test_bad.nrx
o=$(./nerx_linux test $T 2>&1 | tail -1)
cek nerx_test "$o" "1 passed, 1 failed"
cek nerx_check "$(./nerx_linux check $T/h.nrx)" "OK: $T/h.nrx has no errors"
printf '1 + 2\nbe x = 5\nx * 2\n:quit\n' > $T/r.txt
cek nerx_repl "$(./nerx_linux repl < $T/r.txt | tr -d '\n ')" "NerxREPL.:helpforhelp,:quittoexit.>3>>10>"
cek nerx_args "$(printf 'print args()[1]\n' > $T/a.nrx; ./nerx_linux run $T/a.nrx x 'y z' 2>&1)" "y z"
# http (libcurl) ke server lokal python, bila tersedia
if command -v python3 > /dev/null; then
  mkdir -p $T/www; echo "<html>halo nerx http</html>" > $T/www/index.html
  (cd $T/www; python3 -m http.server 8766 --bind 127.0.0.1 > /dev/null 2>&1 & echo $! > $T/pid)
  sleep 1
  printf 'lib nrx_http\nprint httpGet("http://127.0.0.1:8766/")\nprint httpStatus()\n' > $T/http.nrx
  cek nerx_http "$(./nerx_linux run $T/http.nrx | tr -d '\n')" "<html>halo nerx http</html>200"
  kill $(cat $T/pid)
fi
# keyboard: terminal semu lewat script
if command -v script > /dev/null; then
  printf 'lib nrx_keyboard\nprint getKey()\nprint getKey()\n' > $T/kb.nrx
  ./nerx_linux build $T/kb.nrx -o $T/kb > /dev/null
  o=$( (sleep 1; printf 'x'; sleep 0.3; printf '\033[B'; sleep 0.5) | timeout 20 script -qec $T/kb /dev/null | tr -d '\r\n')
  cek nerx_keyboard "$o" "xdown"
fi
rm -rf $T
echo "gagal=$gagal"