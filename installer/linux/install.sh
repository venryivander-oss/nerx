#!/bin/sh
# Pasang nerx dari paket tar.gz ini.
#   ./install.sh                 pasang untuk pengguna ini (~/.local), atau /usr/local kalau dijalankan sebagai root
#   ./install.sh --prefix /opt   pasang ke folder lain (bin/nerx dan lib/nerx di bawahnya)
#   ./install.sh --uninstall     hapus
set -e
AWAL=$(cd "$(dirname "$0")" && pwd)
PREFIX=""
HAPUS=0
while [ $# -gt 0 ]; do
    case "$1" in
        --prefix) PREFIX="$2"; shift 2 ;;
        --uninstall) HAPUS=1; shift ;;
        *) echo "pemakaian: $0 [--prefix folder] [--uninstall]"; exit 1 ;;
    esac
done
if [ -z "$PREFIX" ]; then
    if [ "$(id -u)" = "0" ]; then PREFIX=/usr/local; else PREFIX="$HOME/.local"; fi
fi
TUJUAN="$PREFIX/lib/nerx"
if [ "$HAPUS" = "1" ]; then
    rm -f "$PREFIX/bin/nerx"
    rm -rf "$TUJUAN"
    echo "nerx dihapus dari $PREFIX"
    exit 0
fi
mkdir -p "$TUJUAN" "$PREFIX/bin"
cp "$AWAL/nerx" "$AWAL/prelude.nrx" "$AWAL/pustaka.nrx" "$TUJUAN/"
rm -rf "$TUJUAN/lib"
cp -r "$AWAL/lib" "$TUJUAN/lib"
for f in SYNTAX.txt SYNTAX.en.txt README.md LICENSE contoh_fitur.nrx; do
    [ -f "$AWAL/$f" ] && cp "$AWAL/$f" "$TUJUAN/"
done
chmod 755 "$TUJUAN/nerx"
ln -sf "$TUJUAN/nerx" "$PREFIX/bin/nerx"
echo "nerx terpasang: $PREFIX/bin/nerx"
case ":$PATH:" in
    *":$PREFIX/bin:"*) ;;
    *) echo "tambahkan $PREFIX/bin ke PATH, mis. di ~/.profile:  export PATH=\"$PREFIX/bin:\$PATH\"" ;;
esac