#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
// header mingw lama belum punya konstanta ini (nilainya dari Windows SDK)
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#endif
#include "nrx_console.h"

int nrxConsoleOwns(const char *name){
    return !strcmp(name,"clear") || !strcmp(name,"cursor") || !strcmp(name,"color")
        || !strcmp(name,"runCommand") || !strcmp(name,"commandStatus");
}

static int lastCmdStatus=0;

static int colorUsed=0;

static void resetColor(void){
    printf("\x1b[0m");
    fflush(stdout);
}

// nyalakan kode ANSI di console; kalau output bukan console (mis. dialihkan ke file), tidak melakukan apa-apa
static int consoleReady(void){
    static int ready=-1;
    if(ready>=0) return ready;
    #ifdef _WIN32
    HANDLE h=GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    ready=GetConsoleMode(h,&mode) ? 1 : 0;
    if(ready) SetConsoleMode(h,mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    #else
    ready=1;
    #endif
    return ready;
}

// "merah" -> kode warna ANSI teks; latar = kode teks + 10
static void colorCode(const char *fn, Value v, int background, char *out, int size){
    static const struct { const char *name; int code; } names[]={
        {"hitam",30}, {"black",30}, {"merah",91}, {"red",91},
        // kuning pakai versi normal (33): kuning terang di palet console Windows hampir putih
        {"hijau",92}, {"green",92}, {"kuning",33}, {"yellow",33},
        {"biru",94}, {"blue",94}, {"ungu",95}, {"purple",95},
        {"cyan",96}, {"tosca",96}, {"putih",97}, {"white",97},
        {"abu",90}, {"gray",90},
    };
    char *s=valToStr(v);
    char low[64];
    int k=0;
    for(; s[k] && k<63; k++) low[k]=tolower((unsigned char)s[k]);
    low[k]=0;
    for(size_t i=0;i<sizeof names/sizeof names[0];i++){
        if(strcmp(low,names[i].name)==0){
            snprintf(out,size,"%d",names[i].code+(background ? 10 : 0));
            free(s);
            return;
        }
    }
    unsigned rgb;
    if(low[0]=='#' && strlen(low)==7 && sscanf(low+1,"%6x",&rgb)==1){
        snprintf(out,size,"%d;2;%u;%u;%u",background ? 48 : 38,(rgb>>16)&255,(rgb>>8)&255,rgb&255);
        free(s);
        return;
    }
    nrxFail("%s: warna \"%s\" tidak dikenal, pakai hitam, merah, hijau, kuning, biru, ungu, cyan, putih, abu, atau #RRGGBB",fn,s);
}

Value nrxConsoleCall(const char *name, Value *a, int n){
    fflush(stdout);
    if(!strcmp(name,"commandStatus")){
        if(n!=0) nrxFail("commandStatus tidak butuh argumen: commandStatus()");
        return valNum(lastCmdStatus);
    }
    if(!strcmp(name,"runCommand")){
        // jalankan perintah shell, hasilnya output (stdout + stderr) sebagai teks
        if(n!=1) nrxFail("runCommand butuh 1 argumen: runCommand(perintah)");
        char *cmd=valToStr(a[0]);
        char *full=malloc(strlen(cmd)+16);
        sprintf(full,"%s 2>&1",cmd);
        free(cmd);
        #ifdef _WIN32
        FILE *p=_popen(full,"rb");
        #else
        FILE *p=popen(full,"r");
        #endif
        free(full);
        if(!p) nrxFail("runCommand: perintah tidak bisa dijalankan");
        size_t len=0, cap=4096, got;
        char *out=malloc(cap);
        while((got=fread(out+len,1,cap-len-1,p))>0){
            len+=got;
            if(cap-len-1==0) out=realloc(out,cap*=2);
        }
        out[len]=0;
        #ifdef _WIN32
        lastCmdStatus=_pclose(p);
        #else
        lastCmdStatus=pclose(p);
        #endif
        // CRLF jadi LF supaya sama dengan teks lain di nrxl
        char *w=out;
        for(char *s=out;*s;s++) if(!(s[0]=='\r' && s[1]=='\n')) *w++=*s;
        *w=0;
        Value v=valStr(out);
        free(out);
        return v;
    }
    if(!strcmp(name,"clear")){
        if(n!=0) nrxFail("clear tidak butuh argumen: clear()");
        if(consoleReady()){
            printf("\x1b[2J\x1b[3J\x1b[H");
            fflush(stdout);
        }
        return valStr("");
    }
    if(!strcmp(name,"cursor")){
        if(n!=2) nrxFail("cursor butuh 2 argumen: cursor(x, y)");
        double x, y;
        if(!numLike(a[0],&x) || !numLike(a[1],&y)) nrxFail("cursor(x, y) butuh angka");
        if(x<0) x=0;
        if(y<0) y=0;
        if(consoleReady()){
            printf("\x1b[%d;%dH",(int)y+1,(int)x+1);
            fflush(stdout);
        }
        return valStr("");
    }
    // color()
    if(n>2) nrxFail("color butuh 0 sampai 2 argumen: color(teks, latar)");
    char fg[64]="", bg[64]="";
    if(n>=1) colorCode(name,a[0],0,fg,sizeof fg);
    if(n==2) colorCode(name,a[1],1,bg,sizeof bg);
    if(consoleReady()){
        if(!colorUsed){
            // warna console dikembalikan normal saat program selesai
            atexit(resetColor);
            colorUsed=1;
        }
        printf("\x1b[0m");
        if(fg[0]) printf("\x1b[%sm",fg);
        if(bg[0]) printf("\x1b[%sm",bg);
        fflush(stdout);
    }
    return valStr("");
}
