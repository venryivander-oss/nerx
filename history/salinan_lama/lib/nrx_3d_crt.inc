// nrx_3d_crt.inc: dua fungsi C runtime yang dipakai libraylib.a resmi (dibuat dengan
// MinGW-w64 yang memakai UCRT), tapi tidak ada di msvcrt milik gcc 8.1 yang ikut installer.
// Dimasukkan ke nrx_3d.c, supaya selalu ikut setiap kali raylib ikut.
#ifdef _WIN32
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <sys/stat.h>

static void nrxWassert(const wchar_t *msg, const wchar_t *file, unsigned line){
    fwprintf(stderr,L"Assertion failed: %ls, file %ls, line %u\n",msg,file,line);
    abort();
}
void (*__imp__wassert)(const wchar_t*,const wchar_t*,unsigned)=nrxWassert;

int stat64i32(const char *path, struct _stat64i32 *out){
    struct _stat64 s;
    int r=_stat64(path,&s);
    if(r) return r;
    memset(out,0,sizeof *out);
    out->st_dev=s.st_dev;
    out->st_ino=s.st_ino;
    out->st_mode=s.st_mode;
    out->st_nlink=s.st_nlink;
    out->st_uid=s.st_uid;
    out->st_gid=s.st_gid;
    out->st_rdev=s.st_rdev;
    out->st_size=(_off_t)s.st_size;
    out->st_atime=s.st_atime;
    out->st_mtime=s.st_mtime;
    out->st_ctime=s.st_ctime;
    return 0;
}
#endif
