#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "nrx_keyboard.h"

int nrxKeyboardOwns(const char *name){
    return !strcmp(name,"keyPressed") || !strcmp(name,"getKey");
}

#ifdef _WIN32
static const struct { const char *name; int vk; } keys[]={
    {"left",VK_LEFT}, {"right",VK_RIGHT}, {"up",VK_UP}, {"down",VK_DOWN},
    {"space",VK_SPACE}, {"enter",VK_RETURN}, {"escape",VK_ESCAPE}, {"esc",VK_ESCAPE},
    {"tab",VK_TAB}, {"backspace",VK_BACK}, {"delete",VK_DELETE},
    {"shift",VK_SHIFT}, {"ctrl",VK_CONTROL}, {"alt",VK_MENU},
    {"home",VK_HOME}, {"end",VK_END}, {"pageup",VK_PRIOR}, {"pagedown",VK_NEXT},
};

// nama tombol -> kode tombol Windows (virtual key)
static int keyCode(const char *name){
    char low[32];
    int k=0;
    for(; name[k] && k<31; k++) low[k]=tolower((unsigned char)name[k]);
    low[k]=0;
    // satu huruf atau angka: "a".."z", "0".."9"
    if(k==1 && isalnum((unsigned char)low[0])) return toupper((unsigned char)low[0]);
    // f1..f12
    if(low[0]=='f' && k>=2 && k<=3 && isdigit((unsigned char)low[1])){
        int f=atoi(low+1);
        if(f>=1 && f<=12) return VK_F1+f-1;
    }
    for(size_t i=0;i<sizeof keys/sizeof keys[0];i++){
        if(strcmp(low,keys[i].name)==0) return keys[i].vk;
    }
    return 0;
}

static HANDLE hin;
static int consoleReady=-1;
static unsigned char pressed[256];  // ada tekanan sejak dicek terakhir
static unsigned char held[256];     // sedang ditahan
static unsigned char sawUp[256];    // console ini pernah mengirim "tombol dilepas" untuk tombol ini

// ambil semua kejadian tombol yang menumpuk di antrean input console, tanpa menunggu
static void drainConsole(){
    if(consoleReady<0){
        DWORD mode;
        hin=GetStdHandle(STD_INPUT_HANDLE);
        consoleReady=GetConsoleMode(hin,&mode) ? 1 : 0;
    }
    if(!consoleReady) return;
    DWORD n;
    while(GetNumberOfConsoleInputEvents(hin,&n) && n>0){
        INPUT_RECORD rec[64];
        DWORD got;
        if(!ReadConsoleInputA(hin,rec,64,&got)) break;
        for(DWORD i=0;i<got;i++){
            if(rec[i].EventType!=KEY_EVENT) continue;
            KEY_EVENT_RECORD *k=&rec[i].Event.KeyEvent;
            int vk=k->wVirtualKeyCode & 255;
            if(k->bKeyDown){
                pressed[vk]=1;
                held[vk]=1;
            } else {
                held[vk]=0;
                sawUp[vk]=1;
            }
        }
    }
}

// kode tombol -> nama tombol yang sama dengan nama untuk keyPressed; "" kalau tidak dikenal
static void keyName(KEY_EVENT_RECORD *k, char *out, int size){
    int vk=k->wVirtualKeyCode;
    out[0]=0;
    if((vk>='A' && vk<='Z') || (vk>='0' && vk<='9')){
        snprintf(out,size,"%c",tolower(vk));
        return;
    }
    if(vk>=VK_F1 && vk<=VK_F12){
        snprintf(out,size,"f%d",vk-VK_F1+1);
        return;
    }
    for(size_t i=0;i<sizeof keys/sizeof keys[0];i++){
        if(keys[i].vk==vk){
            snprintf(out,size,"%s",keys[i].name);
            return;
        }
    }
    // tanda baca seperti ! ? , . dikembalikan apa adanya
    char c=k->uChar.AsciiChar;
    if(c>32 && c<127) snprintf(out,size,"%c",c);
}

// tunggu sampai satu tombol ditekan, lalu kembalikan namanya
static Value waitKey(void){
    fflush(stdout);
    drainConsole();
    if(!consoleReady) nrxFail("getKey butuh console (tidak bisa dipakai kalau input dialihkan dari file)");
    for(;;){
        INPUT_RECORD rec;
        DWORD got;
        if(!ReadConsoleInputA(hin,&rec,1,&got)) nrxFail("getKey: gagal membaca keyboard");
        if(rec.EventType!=KEY_EVENT || !rec.Event.KeyEvent.bKeyDown) continue;
        int vk=rec.Event.KeyEvent.wVirtualKeyCode;
        // Shift/Ctrl/Alt sendirian dilewati, supaya Shift+A tetap terbaca sebagai "a"
        if(vk==VK_SHIFT || vk==VK_CONTROL || vk==VK_MENU) continue;
        char name[32];
        keyName(&rec.Event.KeyEvent,name,sizeof name);
        if(name[0]) return valStr(name);
    }
}
#endif

Value nrxKeyboardCall(const char *name, Value *a, int n){
    if(!strcmp(name,"getKey")){
        if(n!=0) nrxFail("getKey tidak butuh argumen: getKey()");
        #ifdef _WIN32
        return waitKey();
        #else
        nrxFail("getKey hanya didukung di Windows");
        #endif
    }
    if(n!=1) nrxFail("keyPressed butuh 1 argumen: keyPressed(\"left\")");
    char *key=valToStr(a[0]);
    #ifdef _WIN32
    int vk=keyCode(key);
    if(!vk) nrxFail("tombol \"%s\" tidak dikenal, contoh: \"a\", \"5\", \"left\", \"space\", \"enter\", \"f1\"",key);
    free(key);
    drainConsole();
    // "sedang ditahan" hanya dipercaya kalau console ini memang mengirim kejadian tombol dilepas;
    // kalau tidak, cukup pakai tekanan (termasuk tekanan berulang saat tombol ditahan)
    int r=pressed[vk] || (sawUp[vk] && held[vk]);
    pressed[vk]=0;
    return valBool(r);
    #else
    free(key);
    nrxFail("keyPressed hanya didukung di Windows");
    return valBool(0);
    #endif
}
