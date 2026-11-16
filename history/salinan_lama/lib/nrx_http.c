#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif
#include "nrx_http.h"

static int lastStatus=0;

int nrxHttpOwns(const char *name){
    return !strcmp(name,"httpGet") || !strcmp(name,"httpPost") || !strcmp(name,"httpStatus")
        || !strcmp(name,"httpStart") || !strcmp(name,"httpDone") || !strcmp(name,"httpResult")
        || !strcmp(name,"httpPartial");
}

#ifdef _WIN32
// winhttp.dll dimuat saat runtime, supaya tidak perlu flag -lwinhttp saat build
#define WH(f) static __typeof__(&f) p##f
WH(WinHttpOpen); WH(WinHttpCrackUrl); WH(WinHttpConnect); WH(WinHttpOpenRequest);
WH(WinHttpSendRequest); WH(WinHttpReceiveResponse); WH(WinHttpQueryHeaders);
WH(WinHttpQueryDataAvailable); WH(WinHttpReadData); WH(WinHttpCloseHandle); WH(WinHttpSetTimeouts);

static void loadWinhttp(void){
    static int loaded=0;
    if(loaded) return;
    HMODULE m=LoadLibraryA("winhttp.dll");
    if(!m) nrxFail("http tidak bisa dipakai: winhttp.dll tidak tersedia");
    #define LOAD(f) if(!(p##f=(void*)GetProcAddress(m,#f))) nrxFail("http tidak bisa dipakai: winhttp.dll tidak tersedia")
    LOAD(WinHttpOpen); LOAD(WinHttpCrackUrl); LOAD(WinHttpConnect); LOAD(WinHttpOpenRequest);
    LOAD(WinHttpSendRequest); LOAD(WinHttpReceiveResponse); LOAD(WinHttpQueryHeaders);
    LOAD(WinHttpQueryDataAvailable); LOAD(WinHttpReadData); LOAD(WinHttpCloseHandle); LOAD(WinHttpSetTimeouts);
    loaded=1;
}

static wchar_t *wide(const char *s){
    int n=MultiByteToWideChar(CP_UTF8,0,s,-1,NULL,0);
    wchar_t *w=malloc(sizeof(wchar_t)*(n>0 ? n : 1));
    if(n<=0) w[0]=0;
    else MultiByteToWideChar(CP_UTF8,0,s,-1,w,n);
    return w;
}

// satu permintaan; body NULL = GET. Hasil: isi balasan (teks) dan *status.
// Kalau gagal: NULL, *errKind 1 = alamat salah, 2 = tidak terhubung (kode di *errCode).
// Tidak memanggil nrxFail, supaya aman dipanggil dari thread. onData (boleh NULL) dipanggil
// untuk tiap potongan balasan yang baru datang (dipakai httpPartial).
static char *requestRaw(const char *fn, const char *url, const char *body, const char *ctype, const char *extraHdr,
        int *status, int *errKind, unsigned long *errCode, void (*onData)(const char *, size_t)){
    wchar_t *wurl=wide(url);
    URL_COMPONENTS u;
    memset(&u,0,sizeof u);
    u.dwStructSize=sizeof u;
    wchar_t host[256], path[2048];
    u.lpszHostName=host;
    u.dwHostNameLength=256;
    u.lpszUrlPath=path;
    u.dwUrlPathLength=2048;
    wchar_t extra[2048];
    u.lpszExtraInfo=extra;
    u.dwExtraInfoLength=2048;
    if(!pWinHttpCrackUrl(wurl,0,0,&u)){
        free(wurl);
        *errKind=1;
        return NULL;
    }
    free(wurl);
    // path + ?query
    wchar_t full[4096];
    _snwprintf(full,4096,L"%.*ls%.*ls",(int)u.dwUrlPathLength,path,(int)u.dwExtraInfoLength,extra);
    full[4095]=0;
    if(!full[0]) wcscpy(full,L"/");
    HINTERNET s=pWinHttpOpen(L"nrxl",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    // batas tunggu jawaban 5 menit (bawaan 30 detik terlalu pendek untuk model AI yang berpikir dulu)
    if(s) pWinHttpSetTimeouts(s,0,60000,60000,300000);
    HINTERNET c=s ? pWinHttpConnect(s,host,u.nPort,0) : NULL;
    HINTERNET r=c ? pWinHttpOpenRequest(c,body ? L"POST" : L"GET",full,NULL,WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,u.nScheme==INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : NULL;
    int ok=r!=NULL;
    if(ok){
        size_t hl=(extraHdr ? strlen(extraHdr) : 0)+300;
        char *h8=malloc(hl);
        h8[0]=0;
        if(body) snprintf(h8,hl,"Content-Type: %s\r\n",ctype ? ctype : "text/plain; charset=utf-8");
        if(extraHdr) strcat(h8,extraHdr);
        wchar_t *hdr=wide(h8);
        free(h8);
        DWORD blen=body ? (DWORD)strlen(body) : 0;
        ok=pWinHttpSendRequest(r,hdr[0] ? hdr : WINHTTP_NO_ADDITIONAL_HEADERS,(DWORD)-1L,
            (LPVOID)body,blen,blen,0) && pWinHttpReceiveResponse(r,NULL);
        free(hdr);
    }
    if(!ok){
        DWORD e=GetLastError();
        if(r) pWinHttpCloseHandle(r);
        if(c) pWinHttpCloseHandle(c);
        if(s) pWinHttpCloseHandle(s);
        *errKind=2;
        *errCode=(unsigned long)e;
        return NULL;
    }
    DWORD st=0, sz=sizeof st;
    pWinHttpQueryHeaders(r,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&st,&sz,WINHTTP_NO_HEADER_INDEX);
    *status=(int)st;
    size_t len=0, cap=4096;
    char *out=malloc(cap);
    for(;;){
        DWORD avail=0;
        if(!pWinHttpQueryDataAvailable(r,&avail) || avail==0) break;
        if(len+avail+1>cap){
            while(len+avail+1>cap) cap*=2;
            out=realloc(out,cap);
        }
        DWORD got=0;
        if(!pWinHttpReadData(r,out+len,avail,&got) || got==0) break;
        if(onData) onData(out+len,got);
        len+=got;
    }
    out[len]=0;
    pWinHttpCloseHandle(r);
    pWinHttpCloseHandle(c);
    pWinHttpCloseHandle(s);
    return out;
}

static void failRequest(const char *fn, const char *url, int kind, unsigned long code){
    if(kind==1) nrxFail("%s: alamat tidak valid: %s (contoh: https://example.com)",fn,url);
    nrxFail("%s: tidak bisa terhubung ke %s (kode %lu)",fn,url,code);
}

static char *request(const char *fn, const char *url, const char *body, const char *ctype, const char *extraHdr){
    loadWinhttp();
    int kind=0;
    unsigned long code=0;
    char *out=requestRaw(fn,url,body,ctype,extraHdr,&lastStatus,&kind,&code,NULL);
    if(!out) failRequest(fn,url,kind,code);
    return out;
}

// httpStart: satu permintaan POST di thread lain, supaya window tetap hidup selama menunggu
static struct {
    char *url, *body, *ctype, *extra;
    char *result;
    int status;
    int errKind;
    unsigned long errCode;
    volatile LONG done;
    HANDLE thread;
    // potongan yang sudah datang tapi belum diambil httpPartial (dijaga partLock)
    char *part;
    size_t partLen, partCap;
} bg;
static CRITICAL_SECTION partLock;
static int partLockReady;

static void bgData(const char *d, size_t n){
    EnterCriticalSection(&partLock);
    if(bg.partLen+n+1>bg.partCap){
        while(bg.partLen+n+1>bg.partCap) bg.partCap=bg.partCap ? bg.partCap*2 : 4096;
        bg.part=realloc(bg.part,bg.partCap);
    }
    memcpy(bg.part+bg.partLen,d,n);
    bg.partLen+=n;
    LeaveCriticalSection(&partLock);
}

static DWORD WINAPI bgRun(LPVOID unused){
    (void)unused;
    bg.result=requestRaw("httpStart",bg.url,bg.body,bg.ctype,bg.extra,&bg.status,&bg.errKind,&bg.errCode,bgData);
    InterlockedExchange(&bg.done,1);
    return 0;
}

static void bgClear(void){
    free(bg.url); free(bg.body); free(bg.ctype); free(bg.extra); free(bg.result);
    bg.url=bg.body=bg.ctype=bg.extra=bg.result=NULL;
    free(bg.part);
    bg.part=NULL;
    bg.partLen=bg.partCap=0;
    if(bg.thread) CloseHandle(bg.thread);
    bg.thread=NULL;
}
#endif

// header tambahan dari map: "Kunci: nilai\r\n" per isi
static char *headerText(const char *name, Value h){
    if(h.type!=T_MAP) nrxFail("%s: header harus map, mis. {\"Authorization\": \"Bearer kunci\"}",name);
    size_t cap=1, len=0;
    char *extra=malloc(cap);
    extra[0]=0;
    for(int i=0;i<h.map->count;i++){
        char *v=valToStr(h.map->vals[i]);
        const char *k=h.map->keys[i];
        size_t need=strlen(k)+strlen(v)+5;
        extra=realloc(extra,cap+=need);
        len+=sprintf(extra+len,"%s: %s\r\n",k,v);
        free(v);
    }
    return extra;
}

Value nrxHttpCall(const char *name, Value *a, int n){
    if(!strcmp(name,"httpStatus")){
        if(n!=0) nrxFail("httpStatus tidak butuh argumen: httpStatus()");
        return valNum(lastStatus);
    }
    #ifdef _WIN32
    if(!strcmp(name,"httpStart")){
        if(n<2 || n>4) nrxFail("httpStart butuh 2 sampai 4 argumen: httpStart(url, isi, jenis, header)");
        if(bg.thread) nrxFail("httpStart: permintaan sebelumnya belum diambil; tunggu httpDone() lalu httpResult()");
        loadWinhttp();
        bg.url=valToStr(a[0]);
        bg.body=valToStr(a[1]);
        bg.ctype=n>=3 ? valToStr(a[2]) : NULL;
        bg.extra=n>=4 ? headerText(name,a[3]) : NULL;
        bg.errKind=0;
        bg.done=0;
        if(!partLockReady){
            InitializeCriticalSection(&partLock);
            partLockReady=1;
        }
        bg.thread=CreateThread(NULL,0,bgRun,NULL,0,NULL);
        if(!bg.thread){
            bgClear();
            nrxFail("httpStart: thread tidak bisa dibuat");
        }
        return valStr("");
    }
    // teks yang datang sejak httpPartial terakhir (untuk jawaban streaming); httpResult
    // tetap mengembalikan seluruh balasan
    if(!strcmp(name,"httpPartial")){
        if(n!=0) nrxFail("httpPartial tidak butuh argumen: httpPartial()");
        if(!bg.thread) return valStr("");
        EnterCriticalSection(&partLock);
        char *t=malloc(bg.partLen+1);
        memcpy(t,bg.part ? bg.part : "",bg.partLen);
        t[bg.partLen]=0;
        bg.partLen=0;
        LeaveCriticalSection(&partLock);
        Value v=valStr(t);
        free(t);
        return v;
    }
    if(!strcmp(name,"httpDone")){
        if(n!=0) nrxFail("httpDone tidak butuh argumen: httpDone()");
        return valBool(!bg.thread || bg.done);
    }
    if(!strcmp(name,"httpResult")){
        if(n!=0) nrxFail("httpResult tidak butuh argumen: httpResult()");
        if(!bg.thread) nrxFail("httpResult: belum ada permintaan; panggil httpStart dulu");
        if(!bg.done) nrxFail("httpResult: permintaan belum selesai; cek httpDone() dulu");
        WaitForSingleObject(bg.thread,INFINITE);
        if(!bg.result){
            char url[2048];
            snprintf(url,sizeof url,"%s",bg.url);
            int kind=bg.errKind;
            unsigned long code=bg.errCode;
            bgClear();
            failRequest("httpStart",url,kind,code);
        }
        lastStatus=bg.status;
        Value v=valStr(bg.result);
        bgClear();
        return v;
    }
    #endif
    int post=!strcmp(name,"httpPost");
    if(!post && n!=1 && n!=2) nrxFail("httpGet butuh 1 atau 2 argumen: httpGet(url) atau httpGet(url, header)");
    if(post && (n<2 || n>4)) nrxFail("httpPost butuh 2 sampai 4 argumen: httpPost(url, isi, jenis, header)");
    #ifdef _WIN32
    char *url=valToStr(a[0]);
    char *body=post ? valToStr(a[1]) : NULL;
    char *ctype=post && n>=3 ? valToStr(a[2]) : NULL;
    // header tambahan: map teks, mis. {"Authorization": "Bearer ..."}
    char *extra=NULL;
    int hi=post ? 3 : 1;
    if(n>hi) extra=headerText(name,a[hi]);
    char *res=request(name,url,body,ctype,extra);
    free(extra);
    free(url);
    free(body);
    free(ctype);
    Value v=valStr(res);
    free(res);
    return v;
    #else
    nrxFail("%s hanya didukung di Windows",name);
    return valStr("");
    #endif
}
