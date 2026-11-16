#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "nrx_sound.h"

int nrxSoundOwns(const char *name){
    return !strcmp(name,"playSound") || !strcmp(name,"playMusic")
        || !strcmp(name,"stopMusic") || !strcmp(name,"stopSound");
}

#ifdef _WIN32
// winmm.dll dimuat saat runtime, supaya tidak perlu flag -lwinmm saat build
static MCIERROR (WINAPI *pMciSendStringA)(LPCSTR,LPSTR,UINT,HWND);
static BOOL (WINAPI *pPlaySoundA)(LPCSTR,HMODULE,DWORD);

static void loadWinmm(void){
    static int loaded=0;
    if(loaded) return;
    HMODULE m=LoadLibraryA("winmm.dll");
    if(m){
        pMciSendStringA=(void*)GetProcAddress(m,"mciSendStringA");
        pPlaySoundA=(void*)GetProcAddress(m,"PlaySoundA");
    }
    if(!pMciSendStringA || !pPlaySoundA) nrxFail("suara tidak bisa diputar: winmm.dll tidak tersedia");
    loaded=1;
}

static int mci(const char *cmd){
    return pMciSendStringA(cmd,NULL,0,NULL)==0;
}

static int isWav(const char *path){
    const char *dot=strrchr(path,'.');
    if(!dot) return 0;
    char ext[8];
    int k=0;
    for(dot++; *dot && k<7; dot++) ext[k++]=tolower((unsigned char)*dot);
    ext[k]=0;
    return !strcmp(ext,"wav");
}

static void needFile(const char *fn, const char *path){
    FILE *f=fopen(path,"rb");
    if(!f) nrxFail("%s: file suara %s tidak ditemukan",fn,path);
    fclose(f);
}

// tiap file efek dibuka sekali (alias sfx0, sfx1, ...), lalu diputar ulang dari awal setiap dipanggil
static char sfxPath[64][260];
static int sfxCount=0;
static int musicMci=0;   // musik sedang diputar lewat MCI (mp3), bukan PlaySound (wav)

static void playEffect(const char *path){
    int id=-1;
    for(int i=0;i<sfxCount;i++) if(!strcmp(sfxPath[i],path)) id=i;
    char cmd[400];
    if(id<0){
        if(sfxCount>=64) nrxFail("playSound: terlalu banyak file suara berbeda (maksimal 64)");
        id=sfxCount;
        snprintf(cmd,sizeof cmd,"open \"%s\" type %s alias sfx%d",path,isWav(path) ? "waveaudio" : "mpegvideo",id);
        if(!mci(cmd)) nrxFail("playSound: %s tidak bisa diputar (pakai WAV atau MP3)",path);
        snprintf(sfxPath[sfxCount++],sizeof sfxPath[0],"%s",path);
    }
    snprintf(cmd,sizeof cmd,"play sfx%d from 0",id);
    if(!mci(cmd)) nrxFail("playSound: %s tidak bisa diputar",path);
}

static void stopMusicNow(void){
    pPlaySoundA(NULL,NULL,0);
    if(musicMci){
        mci("close music");
        musicMci=0;
    }
}

static void playMusicLoop(const char *path){
    stopMusicNow();
    if(isWav(path)){
        // WAV diulang lewat PlaySound (MCI waveaudio tidak bisa repeat)
        if(!pPlaySoundA(path,NULL,SND_FILENAME|SND_ASYNC|SND_LOOP|SND_NODEFAULT)){
            nrxFail("playMusic: %s tidak bisa diputar",path);
        }
        return;
    }
    char cmd[400];
    snprintf(cmd,sizeof cmd,"open \"%s\" type mpegvideo alias music",path);
    if(!mci(cmd)) nrxFail("playMusic: %s tidak bisa diputar (pakai WAV atau MP3)",path);
    musicMci=1;
    if(!mci("play music repeat")) nrxFail("playMusic: %s tidak bisa diputar",path);
}
#endif

Value nrxSoundCall(const char *name, Value *a, int n){
    #ifdef _WIN32
    loadWinmm();
    if(!strcmp(name,"playSound") || !strcmp(name,"playMusic")){
        if(n!=1) nrxFail("%s butuh 1 argumen: %s(\"file.wav\")",name,name);
        char *path=valToStr(a[0]);
        needFile(name,path);
        if(name[4]=='S') playEffect(path);
        else playMusicLoop(path);
        free(path);
        return valStr("");
    }
    if(n!=0) nrxFail("%s tidak butuh argumen: %s()",name,name);
    if(!strcmp(name,"stopMusic")){
        stopMusicNow();
    } else {
        char cmd[64];
        for(int i=0;i<sfxCount;i++){
            snprintf(cmd,sizeof cmd,"stop sfx%d",i);
            mci(cmd);
        }
    }
    return valStr("");
    #else
    (void)a;
    (void)n;
    nrxFail("%s hanya didukung di Windows",name);
    return valStr("");
    #endif
}
