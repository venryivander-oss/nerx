#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "nrx_window.h"

enum { E_TEXT, E_BUTTON, E_INPUT, E_CHECK, E_SELECT, E_LIST, E_CANVAS, E_IMAGE };

// templateStyle nama( ... ): kumpulan properti tampilan yang bisa dipakai elemen atau window
#define MAX_STYLE_PROPS 16
typedef struct {
    char name[64];
    char prop[MAX_STYLE_PROPS][16];
    char val[MAX_STYLE_PROPS][192];   // nilai berupa kode nrxl, dihitung saat window dibuka
    int count;
} StyleDef;

static StyleDef styles[32];
static int styleCount=0;

static const char *propNames[]={"size","font","bold","italic","color","background","width","height","hover","hoverColor"};

static int findStyle(const char *name){
    for(int i=0;i<styleCount;i++){
        if(strcmp(styles[i].name,name)==0) return i;
    }
    return -1;
}

// satu isi window:
//   newText(text, x, y, ukuranHuruf, warna, style...)
//   newButton(onclick(fungsi()), text, x, y, ukuranHuruf, style...)
//   newInput(variabel, x, y, lebar, ukuranHuruf, style...)
//   newCheck(variabel, "teks", x, y, ukuranHuruf, style...)
//   newSelect(variabel, pilihan, x, y, lebar, ukuranHuruf, style...)
//   newList(variabel, pilihan, x, y, lebar, tinggi, ukuranHuruf, style...)
//   newCanvas(nama, x, y, lebar, tinggi, style...)
//   newImage("file.png", x, y, lebar, tinggi, style...)
typedef struct {
    int kind;
    char text[256];   // teks, nama variabel, nama canvas, atau nama file gambar
    char extra[256];  // newCheck: label; newSelect/newList: list pilihan
    char x[64], y[64];
    char size[64];
    char color[64];
    char width[64];
    char height[64];
    int styles[4];    // templateStyle yang dipakai elemen ini, sesuai urutan
    int nstyles;
    int funcPc;
} Elem;

// onTimer(fungsi(), detik)
typedef struct {
    int funcPc;
    char interval[64];
} TimerDef;

typedef struct {
    char name[64];
    Elem el[32];
    int count;
    int keyFunc;          // onKey(fungsi()), -1 kalau tidak ada
    int mouseFunc;        // onMouse(fungsi()), -1 kalau tidak ada
    TimerDef timers[4];
    int ntimers;
} Template;

static Template templates[32];
static int templateCount=0;

static int findTemplate(const char *name){
    for(int i=0;i<templateCount;i++){
        if(strcmp(templates[i].name,name)==0) return i;
    }
    return -1;
}

// baris penutup ")" milik windowTemplate / templateStyle di baris pc
static int templateEnd(int pc){
    if(strchr(lines[pc],')')) return pc;
    int j=pc+1;
    while(j<lineCount && strcmp(lines[j],")")!=0) j++;
    return j<lineCount ? j : -1;
}

static int isIdent(const char *s){
    if(!(isalpha((unsigned char)*s) || *s=='_')) return 0;
    for(; *s; s++) if(!(isalnum((unsigned char)*s) || *s=='_')) return 0;
    return 1;
}

// "tambah()" -> baris fungsi tambah
static int handlerPc(int li, const char *src){
    char fname[64];
    funcName(src,fname,sizeof fname);
    int pc=findFunc(fname);
    if(pc<0) nrxFailAt(li,"fungsi %s tidak ditemukan, buat dulu dengan: func %s()",fname,fname);
    return pc;
}

// argumen setelah yang wajib: nama templateStyle, atau nilai sesuai urutan slot (mis. ukuranHuruf lalu warna)
static void parseOptional(int li, Elem *el, char a[][256], int from, int na, char *slots[], int nslots, const char *usage){
    int next=0;
    for(int k=from;k<na;k++){
        int st=isIdent(a[k]) ? findStyle(a[k]) : -1;
        if(st>=0){
            if(el->nstyles>=4) nrxFailAt(li,"satu elemen maksimal memakai 4 templateStyle");
            el->styles[el->nstyles++]=st;
        } else {
            if(next>=nslots) nrxFailAt(li,"terlalu banyak argumen, cara menulis: %s",usage);
            snprintf(slots[next],64,"%s",a[k]);
            next++;
        }
    }
}

static void copyArg(char *dst, int size, const char *src){
    snprintf(dst,size,"%s",src);
}

static void parseElem(int li, Elem *el){
    char *t=lines[li];
    char a[12][256];
    memset(el,0,sizeof *el);
    el->funcPc=-1;
    int na=splitArgs(t,a,12);
    if(startsWord(t,"newText")){
        const char *usage="newText(text, x, y, ukuranHuruf, warna, style)";
        if(na<3 || na>12) nrxFailAt(li,"newText butuh minimal 3 argumen: %s",usage);
        el->kind=E_TEXT;
        copyArg(el->text,sizeof el->text,a[0]);
        copyArg(el->x,sizeof el->x,a[1]);
        copyArg(el->y,sizeof el->y,a[2]);
        char *slots[]={el->size,el->color};
        parseOptional(li,el,a,3,na,slots,2,usage);
        return;
    }
    if(startsWord(t,"newButton")){
        const char *usage="newButton(onclick(fungsi()), text, x, y, ukuranHuruf, style)";
        if(na<4 || na>12) nrxFailAt(li,"newButton butuh minimal 4 argumen: %s",usage);
        char *p=strchr(a[0],'(');
        char *e=strrchr(a[0],')');
        if(!startsWord(a[0],"onclick") || !p || !e || e<p) nrxFailAt(li,"argumen pertama newButton harus onclick(fungsi())");
        char inner[256];
        snprintf(inner,sizeof inner,"%.*s",(int)(e-p-1),p+1);
        el->funcPc=handlerPc(li,inner);
        el->kind=E_BUTTON;
        copyArg(el->text,sizeof el->text,a[1]);
        copyArg(el->x,sizeof el->x,a[2]);
        copyArg(el->y,sizeof el->y,a[3]);
        char *slots[]={el->size};
        parseOptional(li,el,a,4,na,slots,1,usage);
        return;
    }
    if(startsWord(t,"newInput")){
        const char *usage="newInput(variabel, x, y, lebar, ukuranHuruf, style)";
        if(na<3 || na>12) nrxFailAt(li,"newInput butuh minimal 3 argumen: %s",usage);
        if(!isIdent(a[0])) nrxFailAt(li,"argumen pertama newInput harus nama variabel, contoh: newInput(nama, 20, 20)");
        el->kind=E_INPUT;
        copyArg(el->text,sizeof el->text,a[0]);
        copyArg(el->x,sizeof el->x,a[1]);
        copyArg(el->y,sizeof el->y,a[2]);
        char *slots[]={el->width,el->size};
        parseOptional(li,el,a,3,na,slots,2,usage);
        return;
    }
    if(startsWord(t,"newCheck")){
        const char *usage="newCheck(variabel, \"teks\", x, y, ukuranHuruf, style)";
        if(na<4 || na>12) nrxFailAt(li,"newCheck butuh minimal 4 argumen: %s",usage);
        if(!isIdent(a[0])) nrxFailAt(li,"argumen pertama newCheck harus nama variabel, contoh: %s",usage);
        el->kind=E_CHECK;
        copyArg(el->text,sizeof el->text,a[0]);
        copyArg(el->extra,sizeof el->extra,a[1]);
        copyArg(el->x,sizeof el->x,a[2]);
        copyArg(el->y,sizeof el->y,a[3]);
        char *slots[]={el->size};
        parseOptional(li,el,a,4,na,slots,1,usage);
        return;
    }
    if(startsWord(t,"newSelect") || startsWord(t,"newList")){
        int isList=startsWord(t,"newList");
        const char *usage=isList ? "newList(variabel, pilihan, x, y, lebar, tinggi, ukuranHuruf, style)"
                                 : "newSelect(variabel, pilihan, x, y, lebar, ukuranHuruf, style)";
        if(na<4 || na>12) nrxFailAt(li,"%s butuh minimal 4 argumen: %s",isList ? "newList" : "newSelect",usage);
        if(!isIdent(a[0])) nrxFailAt(li,"argumen pertama harus nama variabel, contoh: %s",usage);
        el->kind=isList ? E_LIST : E_SELECT;
        copyArg(el->text,sizeof el->text,a[0]);
        copyArg(el->extra,sizeof el->extra,a[1]);
        copyArg(el->x,sizeof el->x,a[2]);
        copyArg(el->y,sizeof el->y,a[3]);
        if(isList){
            char *slots[]={el->width,el->height,el->size};
            parseOptional(li,el,a,4,na,slots,3,usage);
        } else {
            char *slots[]={el->width,el->size};
            parseOptional(li,el,a,4,na,slots,2,usage);
        }
        return;
    }
    if(startsWord(t,"newCanvas")){
        const char *usage="newCanvas(nama, x, y, lebar, tinggi, style)";
        if(na<5 || na>12) nrxFailAt(li,"newCanvas butuh minimal 5 argumen: %s",usage);
        if(!isIdent(a[0])) nrxFailAt(li,"argumen pertama newCanvas harus nama, contoh: %s",usage);
        el->kind=E_CANVAS;
        copyArg(el->text,sizeof el->text,a[0]);
        copyArg(el->x,sizeof el->x,a[1]);
        copyArg(el->y,sizeof el->y,a[2]);
        copyArg(el->width,sizeof el->width,a[3]);
        copyArg(el->height,sizeof el->height,a[4]);
        parseOptional(li,el,a,5,na,NULL,0,usage);
        return;
    }
    if(startsWord(t,"newImage")){
        const char *usage="newImage(\"file.png\", x, y, lebar, tinggi, style)";
        if(na<3 || na>12) nrxFailAt(li,"newImage butuh minimal 3 argumen: %s",usage);
        el->kind=E_IMAGE;
        copyArg(el->text,sizeof el->text,a[0]);
        copyArg(el->x,sizeof el->x,a[1]);
        copyArg(el->y,sizeof el->y,a[2]);
        char *slots[]={el->width,el->height};
        parseOptional(li,el,a,3,na,slots,2,usage);
        return;
    }
    nrxFailAt(li,"di dalam windowTemplate hanya boleh newText, newButton, newInput, newCheck, newSelect, newList, newCanvas, newImage, onKey, onMouse, atau onTimer");
}

int nrxWindowOwns(const char *text){
    return startsWord(text,"windowTemplate") || startsWord(text,"templateStyle")
        || startsWord(text,"newWindow") || startsWord(text,"closeWindow");
}

// nama blok setelah kata kunci: "windowTemplate menu(" atau "windowTemplate(menu"
static void blockName(int li, int keywordLen, char *name, int size){
    char rest[512];
    snprintf(rest,sizeof rest,"%s",lines[li]+keywordLen);
    for(char *c=rest; *c; c++) if(*c=='('||*c==')'||*c==',') *c=' ';
    name[0]=0;
    char fmt[16];
    snprintf(fmt,sizeof fmt,"%%%ds",size-1);
    sscanf(rest,fmt,name);
}

void nrxWindowPrepass(int enabled){
    // templateStyle dibaca duluan, supaya windowTemplate bisa memakainya
    for(int i=0;i<lineCount;i++){
        if(!startsWord(lines[i],"templateStyle")) continue;
        if(!enabled) nrxFailAt(i,"templateStyle butuh library window, tambahkan di atas: lib nrx_window.h");
        char name[64];
        blockName(i,13,name,sizeof name);
        if(!isIdent(name) || styleCount>=32) nrxFailAt(i,"templateStyle butuh nama, contoh: templateStyle judul(");
        if(findStyle(name)>=0) nrxFailAt(i,"templateStyle %s sudah ada",name);
        int end=templateEnd(i);
        if(end<0) nrxFailAt(i,"templateStyle %s tidak ditutup dengan )",name);
        StyleDef *s=&styles[styleCount];
        snprintf(s->name,sizeof s->name,"%s",name);
        s->count=0;
        for(int j=i+1;j<end;j++){
            // properti: nilai
            char *colon=strchr(lines[j],':');
            if(!colon) nrxFailAt(j,"cara menulis isi templateStyle: properti: nilai, contoh: color: \"biru\"");
            if(s->count>=MAX_STYLE_PROPS) nrxFailAt(j,"templateStyle maksimal %d properti",MAX_STYLE_PROPS);
            char prop[64];
            snprintf(prop,sizeof prop,"%.*s",(int)(colon-lines[j]),lines[j]);
            trim(prop);
            int ok=0;
            for(size_t k=0;k<sizeof propNames/sizeof propNames[0];k++) if(!strcmp(prop,propNames[k])) ok=1;
            if(!ok) nrxFailAt(j,"properti %s tidak dikenal (yang ada: size, font, bold, italic, color, background, width, height, hover, hoverColor)",prop);
            snprintf(s->prop[s->count],sizeof s->prop[s->count],"%s",prop);
            snprintf(s->val[s->count],sizeof s->val[s->count],"%s",colon+1);
            trim(s->val[s->count]);
            if(!s->val[s->count][0]) nrxFailAt(j,"nilai properti %s masih kosong",prop);
            s->count++;
        }
        styleCount++;
    }
    for(int i=0;i<lineCount;i++){
        if(!startsWord(lines[i],"windowTemplate")) continue;
        if(!enabled) nrxFailAt(i,"windowTemplate butuh library window, tambahkan di atas: lib nrx_window.h");
        char name[64];
        blockName(i,14,name,sizeof name);
        if(!name[0] || templateCount>=32) nrxFailAt(i,"windowTemplate butuh nama, contoh: windowTemplate menuUtama(");
        if(findStyle(name)>=0) nrxFailAt(i,"nama %s sudah dipakai templateStyle",name);
        int end=templateEnd(i);
        if(end<0) nrxFailAt(i,"windowTemplate %s tidak ditutup dengan )",name);
        Template *t=&templates[templateCount];
        snprintf(t->name,sizeof t->name,"%s",name);
        t->count=0;
        t->keyFunc=-1;
        t->mouseFunc=-1;
        t->ntimers=0;
        for(int j=i+1;j<end;j++){
            char a[3][256];
            if(startsWord(lines[j],"onKey")){
                // onKey(fungsi()): dipanggil setiap tombol keyboard ditekan saat window aktif
                if(splitArgs(lines[j],a,3)!=1) nrxFailAt(j,"cara menulis: onKey(fungsi())");
                if(t->keyFunc>=0) nrxFailAt(j,"satu windowTemplate hanya boleh punya 1 onKey");
                t->keyFunc=handlerPc(j,a[0]);
                continue;
            }
            if(startsWord(lines[j],"onMouse")){
                // onMouse(fungsi()): klik/lepas/gerak mouse di canvas -> fungsi(x, y, aksi, canvas)
                if(splitArgs(lines[j],a,3)!=1) nrxFailAt(j,"cara menulis: onMouse(fungsi())");
                if(t->mouseFunc>=0) nrxFailAt(j,"satu windowTemplate hanya boleh punya 1 onMouse");
                t->mouseFunc=handlerPc(j,a[0]);
                continue;
            }
            if(startsWord(lines[j],"onTimer")){
                // onTimer(fungsi(), detik): dipanggil terus setiap sekian detik
                if(splitArgs(lines[j],a,3)!=2) nrxFailAt(j,"cara menulis: onTimer(fungsi(), detik)");
                if(t->ntimers>=4) nrxFailAt(j,"satu windowTemplate maksimal punya 4 onTimer");
                t->timers[t->ntimers].funcPc=handlerPc(j,a[0]);
                snprintf(t->timers[t->ntimers].interval,sizeof t->timers[t->ntimers].interval,"%s",a[1]);
                t->ntimers++;
                continue;
            }
            if(t->count>=32) nrxFailAt(j,"windowTemplate maksimal 32 isi");
            parseElem(j,&t->el[t->count]);
            t->count++;
        }
        templateCount++;
    }
}

static int evalInt(const char *src){
    Value v=evalExpr(src);
    int r=(int)valToNum(v);
    valFree(&v);
    return r;
}

static int evalTruthy(const char *src){
    Value v=evalExpr(src);
    int r=valTruthy(v);
    valFree(&v);
    return r;
}

#ifdef _WIN32
#define CTL_ID 1000
#define TIMER_ID 500

// tampilan hasil gabungan style: -1 / 0 berarti belum diatur
typedef struct {
    int size;
    char font[64];
    int bold, italic;
    int hasColor;
    COLORREF color;
    int hasBg;
    COLORREF bg;
    int width, height;
    int hasHover;
    COLORREF hover;
    int hasHoverColor;
    COLORREF hoverColor;
} Look;

// bidang gambar: gambar disimpan di memori, lalu ditampilkan ke layar saat window diperbarui
typedef struct {
    char name[64];
    HDC dc;
    HBITMAP bmp;
    int w, h;
    COLORREF bg;
    CRITICAL_SECTION lock;
    volatile int dirty;
    HWND hwnd;
} Canvas;

static Canvas canvases[64];
static int canvasCount=0;

typedef struct {
    HWND hwnd;
    HANDLE ready;
    char title[256];
    int w, h;
    int tpl;
    int hasBg;            // latar window dari templateStyle
    COLORREF bg;
    HBRUSH bgBrush;
    int timerMs[4];
    volatile int timerPending[4];
    volatile int movePending;   // gerakan mouse berikutnya baru dikirim setelah yang sebelumnya selesai
    int ex[32], ey[32], ew[32], eh[32];
    HFONT font[32];
    COLORREF color[32];
    int hasColor[32];
    COLORREF ebg[32];     // latar elemen
    int hasEbg[32];
    HBRUSH ebgBrush[32];
    int ownerDraw[32];    // tombol berwarna digambar sendiri
    COLORREF hover[32];
    int hasHover[32];
    COLORREF hoverColor[32];
    int hasHoverColor[32];
    int hovering[32];
    WNDPROC oldProc[32];
    Canvas *canvas[32];
    char opts[32][1024];  // pilihan newSelect/newList, dipisah \n
    char shown[32][256];
    HWND ctl[32];
} NrxlWin;

static NrxlWin wins[64];
static HANDLE winThreads[64];
static int winCount=0;
static volatile LONG openWins=0;
static HFONT uiFont;
static LOGFONTA baseFont;

// fungsi gdi32 dimuat saat runtime, supaya tidak perlu flag -lgdi32 saat build
static HGDIOBJ (WINAPI *pSelectObject)(HDC,HGDIOBJ);
static BOOL (WINAPI *pGetTextExtentPoint32A)(HDC,LPCSTR,int,LPSIZE);
static int (WINAPI *pSetBkMode)(HDC,int);
static COLORREF (WINAPI *pSetTextColor)(HDC,COLORREF);
static COLORREF (WINAPI *pSetBkColor)(HDC,COLORREF);
static HFONT (WINAPI *pCreateFontIndirectA)(const LOGFONTA*);
static HGDIOBJ (WINAPI *pGetStockObject)(int);
static HBRUSH (WINAPI *pCreateSolidBrush)(COLORREF);
static BOOL (WINAPI *pDeleteObject)(HGDIOBJ);
static HDC (WINAPI *pCreateCompatibleDC)(HDC);
static HBITMAP (WINAPI *pCreateCompatibleBitmap)(HDC,int,int);
static BOOL (WINAPI *pBitBlt)(HDC,int,int,int,int,HDC,int,int,DWORD);
static BOOL (WINAPI *pEllipse)(HDC,int,int,int,int);
static BOOL (WINAPI *pMoveToEx)(HDC,int,int,LPPOINT);
static BOOL (WINAPI *pLineTo)(HDC,int,int);
static HPEN (WINAPI *pCreatePen)(int,int,COLORREF);
static BOOL (WINAPI *pTextOutA)(HDC,int,int,LPCSTR,int);

static int loadGdi(){
    HMODULE g=LoadLibraryA("gdi32.dll");
    if(!g) return 0;
    pSelectObject=(void*)GetProcAddress(g,"SelectObject");
    pGetTextExtentPoint32A=(void*)GetProcAddress(g,"GetTextExtentPoint32A");
    pSetBkMode=(void*)GetProcAddress(g,"SetBkMode");
    pSetTextColor=(void*)GetProcAddress(g,"SetTextColor");
    pSetBkColor=(void*)GetProcAddress(g,"SetBkColor");
    pCreateFontIndirectA=(void*)GetProcAddress(g,"CreateFontIndirectA");
    pGetStockObject=(void*)GetProcAddress(g,"GetStockObject");
    pCreateSolidBrush=(void*)GetProcAddress(g,"CreateSolidBrush");
    pDeleteObject=(void*)GetProcAddress(g,"DeleteObject");
    pCreateCompatibleDC=(void*)GetProcAddress(g,"CreateCompatibleDC");
    pCreateCompatibleBitmap=(void*)GetProcAddress(g,"CreateCompatibleBitmap");
    pBitBlt=(void*)GetProcAddress(g,"BitBlt");
    pEllipse=(void*)GetProcAddress(g,"Ellipse");
    pMoveToEx=(void*)GetProcAddress(g,"MoveToEx");
    pLineTo=(void*)GetProcAddress(g,"LineTo");
    pCreatePen=(void*)GetProcAddress(g,"CreatePen");
    pTextOutA=(void*)GetProcAddress(g,"TextOutA");
    return pSelectObject && pGetTextExtentPoint32A && pSetBkMode && pSetTextColor && pSetBkColor
        && pCreateFontIndirectA && pGetStockObject && pCreateSolidBrush && pDeleteObject
        && pCreateCompatibleDC && pCreateCompatibleBitmap && pBitBlt && pEllipse
        && pMoveToEx && pLineTo && pCreatePen && pTextOutA;
}

// gambar PNG/JPG/BMP lewat GDI+ (juga dimuat saat runtime)
typedef struct {
    UINT32 GdiplusVersion;
    void *DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
} GdipStartupInput;

static int (WINAPI *pGdiplusStartup)(ULONG_PTR*,const GdipStartupInput*,void*);
static int (WINAPI *pGdipLoadImageFromFile)(const WCHAR*,void**);
static int (WINAPI *pGdipCreateFromHDC)(HDC,void**);
static int (WINAPI *pGdipDrawImageRectI)(void*,void*,int,int,int,int);
static int (WINAPI *pGdipGetImageWidth)(void*,UINT*);
static int (WINAPI *pGdipGetImageHeight)(void*,UINT*);
static int (WINAPI *pGdipDeleteGraphics)(void*);

static int gdiplusReady(){
    static int ready=-1;
    if(ready>=0) return ready;
    ready=0;
    HMODULE g=LoadLibraryA("gdiplus.dll");
    if(!g) return 0;
    pGdiplusStartup=(void*)GetProcAddress(g,"GdiplusStartup");
    pGdipLoadImageFromFile=(void*)GetProcAddress(g,"GdipLoadImageFromFile");
    pGdipCreateFromHDC=(void*)GetProcAddress(g,"GdipCreateFromHDC");
    pGdipDrawImageRectI=(void*)GetProcAddress(g,"GdipDrawImageRectI");
    pGdipGetImageWidth=(void*)GetProcAddress(g,"GdipGetImageWidth");
    pGdipGetImageHeight=(void*)GetProcAddress(g,"GdipGetImageHeight");
    pGdipDeleteGraphics=(void*)GetProcAddress(g,"GdipDeleteGraphics");
    if(!pGdiplusStartup || !pGdipLoadImageFromFile || !pGdipCreateFromHDC || !pGdipDrawImageRectI
       || !pGdipGetImageWidth || !pGdipGetImageHeight || !pGdipDeleteGraphics) return 0;
    ULONG_PTR token;
    GdipStartupInput in={1,NULL,FALSE,FALSE};
    if(pGdiplusStartup(&token,&in,NULL)!=0) return 0;
    ready=1;
    return 1;
}

// gambar yang sudah dibuka disimpan, supaya drawImage di dalam loop tidak membaca file berulang
static struct { char path[260]; void *img; UINT w, h; } images[64];
static int imageCount=0;

static int loadImage(const char *path, void **img, UINT *w, UINT *h){
    for(int i=0;i<imageCount;i++){
        if(!strcmp(images[i].path,path)){
            *img=images[i].img;
            *w=images[i].w;
            *h=images[i].h;
            return 1;
        }
    }
    if(!gdiplusReady()) nrxFail("gambar tidak bisa dibuka: GDI+ tidak tersedia");
    WCHAR wpath[260];
    MultiByteToWideChar(CP_ACP,0,path,-1,wpath,260);
    void *im=NULL;
    if(pGdipLoadImageFromFile(wpath,&im)!=0 || !im) nrxFail("gambar %s tidak ditemukan atau formatnya tidak didukung (pakai PNG, JPG, atau BMP)",path);
    pGdipGetImageWidth(im,w);
    pGdipGetImageHeight(im,h);
    *img=im;
    if(imageCount<64){
        snprintf(images[imageCount].path,sizeof images[imageCount].path,"%s",path);
        images[imageCount].img=im;
        images[imageCount].w=*w;
        images[imageCount].h=*h;
        imageCount++;
    }
    return 1;
}

static void paintImage(HDC dc, void *img, int x, int y, int w, int h){
    void *g=NULL;
    if(pGdipCreateFromHDC(dc,&g)!=0) return;
    pGdipDrawImageRectI(g,img,x,y,w,h);
    pGdipDeleteGraphics(g);
}

// font sesuai ukuran/jenis/tebal/miring, disimpan supaya tidak dibuat berulang
static HFONT fontFor(const Look *l){
    static LOGFONTA keys[32];
    static HFONT fonts[32];
    static int count=0;
    if(l->size<=0 && !l->font[0] && l->bold<0 && l->italic<0) return uiFont;
    LOGFONTA lf=baseFont;
    if(l->size>0) lf.lfHeight=-l->size;
    lf.lfWidth=0;
    if(l->font[0]) snprintf(lf.lfFaceName,sizeof lf.lfFaceName,"%s",l->font);
    if(l->bold>=0) lf.lfWeight=l->bold ? FW_BOLD : FW_NORMAL;
    if(l->italic>=0) lf.lfItalic=l->italic ? TRUE : FALSE;
    for(int i=0;i<count;i++){
        if(keys[i].lfHeight==lf.lfHeight && keys[i].lfWeight==lf.lfWeight && keys[i].lfItalic==lf.lfItalic
           && !strcmp(keys[i].lfFaceName,lf.lfFaceName)) return fonts[i];
    }
    HFONT f=pCreateFontIndirectA(&lf);
    if(count<32){
        keys[count]=lf;
        fonts[count]=f;
        count++;
    }
    return f;
}

static int parseColor(const char *s, COLORREF *out){
    static const struct { const char *name; COLORREF rgb; } names[]={
        {"hitam",RGB(0,0,0)},        {"black",RGB(0,0,0)},
        {"putih",RGB(255,255,255)},  {"white",RGB(255,255,255)},
        {"merah",RGB(220,38,38)},    {"red",RGB(220,38,38)},
        {"hijau",RGB(22,163,74)},    {"green",RGB(22,163,74)},
        {"biru",RGB(37,99,235)},     {"blue",RGB(37,99,235)},
        {"kuning",RGB(202,138,4)},   {"yellow",RGB(202,138,4)},
        {"oranye",RGB(234,88,12)},   {"orange",RGB(234,88,12)},
        {"ungu",RGB(147,51,234)},    {"purple",RGB(147,51,234)},
        {"abu",RGB(107,114,128)},    {"gray",RGB(107,114,128)},
    };
    char low[64];
    int k=0;
    for(; s[k] && k<63; k++) low[k]=tolower((unsigned char)s[k]);
    low[k]=0;
    for(size_t i=0;i<sizeof names/sizeof names[0];i++){
        if(strcmp(low,names[i].name)==0){
            *out=names[i].rgb;
            return 1;
        }
    }
    unsigned v;
    if(low[0]=='#' && strlen(low)==7 && sscanf(low+1,"%6x",&v)==1){
        *out=RGB((v>>16)&255,(v>>8)&255,v&255);
        return 1;
    }
    return 0;
}

static COLORREF colorOf(const char *s){
    COLORREF c;
    if(!parseColor(s,&c)) nrxFail("warna %s tidak dikenal, pakai nama (merah, biru, hijau, ...) atau #RRGGBB",s);
    return c;
}

static COLORREF evalColor(const char *src){
    char *cs=evalSeqStr(src);
    COLORREF c=colorOf(cs);
    free(cs);
    return c;
}

static COLORREF darken(COLORREF c){
    return RGB(GetRValue(c)*4/5,GetGValue(c)*4/5,GetBValue(c)*4/5);
}

static void lookInit(Look *l){
    memset(l,0,sizeof *l);
    l->bold=-1;
    l->italic=-1;
}

// terapkan semua properti templateStyle ke tampilan l (properti yang belakangan menimpa yang duluan)
static void lookApply(Look *l, const StyleDef *s){
    for(int i=0;i<s->count;i++){
        const char *p=s->prop[i], *v=s->val[i];
        if(!strcmp(p,"size")) l->size=evalInt(v);
        else if(!strcmp(p,"font")){
            char *f=evalSeqStr(v);
            snprintf(l->font,sizeof l->font,"%s",f);
            free(f);
        }
        else if(!strcmp(p,"bold")) l->bold=evalTruthy(v);
        else if(!strcmp(p,"italic")) l->italic=evalTruthy(v);
        else if(!strcmp(p,"color")){
            l->color=evalColor(v);
            l->hasColor=1;
        }
        else if(!strcmp(p,"background")){
            l->bg=evalColor(v);
            l->hasBg=1;
        }
        else if(!strcmp(p,"width")) l->width=evalInt(v);
        else if(!strcmp(p,"height")) l->height=evalInt(v);
        else if(!strcmp(p,"hover")){
            l->hover=evalColor(v);
            l->hasHover=1;
        }
        else if(!strcmp(p,"hoverColor")){
            l->hoverColor=evalColor(v);
            l->hasHoverColor=1;
        }
    }
}

// event dari thread window (klik, tombol keyboard, mouse, timer) masuk antrian, lalu dijalankan di thread utama
enum { EV_CLICK, EV_KEY, EV_MOUSE, EV_TIMER };

typedef struct {
    int type;
    int fpc;
    char key[16];      // EV_KEY: nama tombol
    int mx, my;        // EV_MOUSE: posisi di canvas
    char action[16];   // EV_MOUSE: left / right / release / move
    char canvas[64];   // EV_MOUSE: nama canvas
    int slot;          // window asal (untuk timer dan gerakan mouse)
    int timer;
} Event;

static CRITICAL_SECTION clickLock;
static HANDLE clickEvent=NULL;
static Event queue[256];
static int qHead=0, qTail=0;

static void pushEvent(const Event *e){
    EnterCriticalSection(&clickLock);
    int next=(qTail+1)%256;
    if(next!=qHead){
        queue[qTail]=*e;
        qTail=next;
    }
    LeaveCriticalSection(&clickLock);
    SetEvent(clickEvent);
}

static void pushSimple(int type, int fpc, int slot, int timer){
    Event e;
    memset(&e,0,sizeof e);
    e.type=type;
    e.fpc=fpc;
    e.slot=slot;
    e.timer=timer;
    pushEvent(&e);
}

static int popEvent(Event *e){
    int ok=0;
    EnterCriticalSection(&clickLock);
    if(qHead!=qTail){
        *e=queue[qHead];
        qHead=(qHead+1)%256;
        ok=1;
    }
    LeaveCriticalSection(&clickLock);
    return ok;
}

// kode tombol -> nama tombol (sama dengan nama di nrx_keyboard)
static void keyName(int vk, char *out, int size){
    static const struct { const char *name; int vk; } keys[]={
        {"left",VK_LEFT}, {"right",VK_RIGHT}, {"up",VK_UP}, {"down",VK_DOWN},
        {"space",VK_SPACE}, {"enter",VK_RETURN}, {"escape",VK_ESCAPE},
        {"tab",VK_TAB}, {"backspace",VK_BACK}, {"delete",VK_DELETE},
        {"shift",VK_SHIFT}, {"ctrl",VK_CONTROL}, {"alt",VK_MENU},
        {"home",VK_HOME}, {"end",VK_END}, {"pageup",VK_PRIOR}, {"pagedown",VK_NEXT},
    };
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
}

// ukuran kontrol mengikuti panjang teks dan fontnya; width/height dari style (kalau ada) menimpanya
static void fitControl(HWND c, const char *t, int kind, int fixW, int fixH){
    if(kind==E_CANVAS || kind==E_IMAGE) return;
    if(kind==E_INPUT || kind==E_SELECT || kind==E_LIST) t="Ay";
    if(!t[0]) t=" ";
    HFONT f=(HFONT)SendMessageA(c,WM_GETFONT,0,0);
    HDC dc=GetDC(c);
    HGDIOBJ old=pSelectObject(dc,f ? (HGDIOBJ)f : (HGDIOBJ)uiFont);
    SIZE s;
    pGetTextExtentPoint32A(dc,t,strlen(t),&s);
    pSelectObject(dc,old);
    ReleaseDC(c,dc);
    int w, h;
    if(kind==E_BUTTON){
        w=s.cx+28;
        h=s.cy+12;
        if(w<75) w=75;
    } else if(kind==E_INPUT){
        w=150;
        h=s.cy+10;
    } else if(kind==E_CHECK){
        w=s.cx+24;
        h=s.cy+6;
    } else if(kind==E_SELECT){
        // tinggi combobox = tinggi daftar pilihan saat dibuka
        w=150;
        h=200;
    } else if(kind==E_LIST){
        w=150;
        h=100;
    } else {
        w=s.cx+2;
        h=s.cy+2;
    }
    if(fixW>0) w=fixW;
    if(fixH>0) h=fixH;
    SetWindowPos(c,NULL,0,0,w,h,SWP_NOMOVE|SWP_NOZORDER);
}

static void winAndIndex(HWND ctl, NrxlWin **win, int *i){
    *win=&wins[GetWindowLongPtrA(GetParent(ctl),GWLP_USERDATA)];
    *i=GetDlgCtrlID(ctl)-CTL_ID;
}

// gambar tombol yang punya warna sendiri (tombol bawaan Windows tidak bisa diberi warna latar)
static void drawButton(NrxlWin *win, DRAWITEMSTRUCT *d){
    int i=d->CtlID-CTL_ID;
    COLORREF bg=win->hasEbg[i] ? win->ebg[i] : GetSysColor(COLOR_BTNFACE);
    COLORREF fg=win->hasColor[i] ? win->color[i] : GetSysColor(COLOR_BTNTEXT);
    if(win->hovering[i]){
        if(win->hasHover[i]) bg=win->hover[i];
        if(win->hasHoverColor[i]) fg=win->hoverColor[i];
    }
    int pressed=d->itemState & ODS_SELECTED;
    if(pressed) bg=darken(bg);
    HBRUSH fill=pCreateSolidBrush(bg);
    FillRect(d->hDC,&d->rcItem,fill);
    pDeleteObject(fill);
    HBRUSH frame=pCreateSolidBrush(darken(darken(bg)));
    FrameRect(d->hDC,&d->rcItem,frame);
    pDeleteObject(frame);
    char txt[256];
    GetWindowTextA(d->hwndItem,txt,sizeof txt);
    pSetBkMode(d->hDC,TRANSPARENT);
    pSetTextColor(d->hDC,fg);
    HFONT f=(HFONT)SendMessageA(d->hwndItem,WM_GETFONT,0,0);
    HGDIOBJ old=pSelectObject(d->hDC,f ? (HGDIOBJ)f : (HGDIOBJ)uiFont);
    RECT r=d->rcItem;
    if(pressed) OffsetRect(&r,1,1);
    DrawTextA(d->hDC,txt,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    pSelectObject(d->hDC,old);
    if(d->itemState & ODS_FOCUS){
        RECT fr=d->rcItem;
        InflateRect(&fr,-3,-3);
        DrawFocusRect(d->hDC,&fr);
    }
}

// tombol berwarna: lacak kursor masuk/keluar untuk efek hover
static LRESULT CALLBACK buttonProc(HWND c, UINT msg, WPARAM wp, LPARAM lp){
    NrxlWin *win;
    int i;
    winAndIndex(c,&win,&i);
    if(msg==WM_MOUSEMOVE && !win->hovering[i]){
        win->hovering[i]=1;
        TRACKMOUSEEVENT tme={sizeof tme,TME_LEAVE,c,0};
        TrackMouseEvent(&tme);
        InvalidateRect(c,NULL,FALSE);
    } else if(msg==WM_MOUSELEAVE){
        win->hovering[i]=0;
        InvalidateRect(c,NULL,FALSE);
    } else if(msg==WM_SETCURSOR && (win->hasHover[i] || win->hasHoverColor[i])){
        SetCursor(LoadCursor(NULL,IDC_HAND));
        return TRUE;
    }
    return CallWindowProcA(win->oldProc[i],c,msg,wp,lp);
}

// canvas: tampilkan gambar dari memori ke layar, dan kirim kejadian mouse ke onMouse
static LRESULT CALLBACK canvasProc(HWND c, UINT msg, WPARAM wp, LPARAM lp){
    Canvas *cv=(Canvas*)GetWindowLongPtrA(c,GWLP_USERDATA);
    if(msg==WM_ERASEBKGND) return 1;
    if(cv && cv->name[0] && (msg==WM_LBUTTONDOWN || msg==WM_RBUTTONDOWN || msg==WM_LBUTTONUP
       || msg==WM_RBUTTONUP || msg==WM_MOUSEMOVE)){
        int slot=(int)GetWindowLongPtrA(GetParent(c),GWLP_USERDATA);
        NrxlWin *win=&wins[slot];
        Template *t=win->tpl>=0 ? &templates[win->tpl] : NULL;
        if(msg==WM_LBUTTONDOWN || msg==WM_RBUTTONDOWN){
            // klik canvas: fokus keyboard kembali ke window (bukan ke tombol/dropdown), dan mouse
            // tetap dilacak walaupun keluar canvas selama tombol mouse ditahan
            SetFocus(GetParent(c));
            SetCapture(c);
        } else if(msg==WM_LBUTTONUP || msg==WM_RBUTTONUP) {
            ReleaseCapture();
        }
        if(t && t->mouseFunc>=0 && !(msg==WM_MOUSEMOVE && win->movePending)){
            Event e;
            memset(&e,0,sizeof e);
            e.type=EV_MOUSE;
            e.fpc=t->mouseFunc;
            e.mx=(short)LOWORD(lp);
            e.my=(short)HIWORD(lp);
            e.slot=slot;
            snprintf(e.canvas,sizeof e.canvas,"%s",cv->name);
            const char *a=msg==WM_LBUTTONDOWN ? "left" : msg==WM_RBUTTONDOWN ? "right"
                        : msg==WM_MOUSEMOVE ? "move" : "release";
            snprintf(e.action,sizeof e.action,"%s",a);
            if(msg==WM_MOUSEMOVE) win->movePending=1;
            pushEvent(&e);
        }
        return 0;
    }
    if(msg==WM_PAINT && cv){
        PAINTSTRUCT ps;
        HDC dc=BeginPaint(c,&ps);
        EnterCriticalSection(&cv->lock);
        pBitBlt(dc,0,0,cv->w,cv->h,cv->dc,0,0,SRCCOPY);
        LeaveCriticalSection(&cv->lock);
        EndPaint(c,&ps);
        return 0;
    }
    return DefWindowProcA(c,msg,wp,lp);
}

static LRESULT CALLBACK nrxlWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp){
    int slot=(int)GetWindowLongPtrA(hwnd,GWLP_USERDATA);
    NrxlWin *win=&wins[slot];
    if(msg==WM_COMMAND && HIWORD(wp)==BN_CLICKED && LOWORD(wp)>=CTL_ID){
        int i=LOWORD(wp)-CTL_ID;
        if(win->tpl>=0 && i<templates[win->tpl].count && templates[win->tpl].el[i].kind==E_BUTTON){
            pushSimple(EV_CLICK,templates[win->tpl].el[i].funcPc,-1,-1);
        }
        return 0;
    }
    if(msg==WM_TIMER && wp>=TIMER_ID && wp<TIMER_ID+4){
        // timer berikutnya baru masuk antrian setelah yang sebelumnya selesai dijalankan
        int k=wp-TIMER_ID;
        if(win->tpl>=0 && k<templates[win->tpl].ntimers && !win->timerPending[k]){
            win->timerPending[k]=1;
            pushSimple(EV_TIMER,templates[win->tpl].timers[k].funcPc,slot,k);
        }
        return 0;
    }
    if(msg==WM_ERASEBKGND && win->hasBg){
        RECT rc;
        GetClientRect(hwnd,&rc);
        FillRect((HDC)wp,&rc,win->bgBrush);
        return 1;
    }
    if(msg==WM_DRAWITEM){
        DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT*)lp;
        int i=d->CtlID-CTL_ID;
        if(d->CtlType==ODT_BUTTON && i>=0 && i<32){
            drawButton(win,d);
            return TRUE;
        }
    }
    if(msg==WM_CTLCOLORSTATIC){
        // latar newText/newCheck: latar elemen, kalau tidak ada pakai latar window
        HDC dc=(HDC)wp;
        int i=GetDlgCtrlID((HWND)lp)-CTL_ID;
        pSetBkMode(dc,TRANSPARENT);
        if(i>=0 && i<32 && win->hasColor[i]) pSetTextColor(dc,win->color[i]);
        if(i>=0 && i<32 && win->hasEbg[i]){
            pSetBkColor(dc,win->ebg[i]);
            return (LRESULT)win->ebgBrush[i];
        }
        if(win->hasBg){
            pSetBkColor(dc,win->bg);
            return (LRESULT)win->bgBrush;
        }
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    if(msg==WM_CTLCOLOREDIT || msg==WM_CTLCOLORLISTBOX){
        HDC dc=(HDC)wp;
        int i=GetDlgCtrlID((HWND)lp)-CTL_ID;
        if(i>=0 && i<32 && (win->hasColor[i] || win->hasEbg[i])){
            if(win->hasColor[i]) pSetTextColor(dc,win->color[i]);
            if(win->hasEbg[i]){
                pSetBkColor(dc,win->ebg[i]);
                return (LRESULT)win->ebgBrush[i];
            }
            pSetBkColor(dc,GetSysColor(COLOR_WINDOW));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
    }
    if(msg==WM_DESTROY){
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd,msg,wp,lp);
}

// isi pilihan newSelect/newList dari teks yang dipisah \n
static void fillOptions(HWND c, int kind, const char *opts){
    UINT reset=kind==E_SELECT ? CB_RESETCONTENT : LB_RESETCONTENT;
    UINT add=kind==E_SELECT ? CB_ADDSTRING : LB_ADDSTRING;
    SendMessageA(c,reset,0,0);
    char buf[1024];
    snprintf(buf,sizeof buf,"%s",opts);
    char *p=buf;
    while(*p){
        char *nl=strchr(p,'\n');
        if(nl) *nl=0;
        SendMessageA(c,add,0,(LPARAM)p);
        if(!nl) break;
        p=nl+1;
    }
}

// pilih item yang teksnya sama dengan text (atau tidak memilih apa-apa)
static void selectText(HWND c, int kind, const char *text){
    UINT find=kind==E_SELECT ? CB_FINDSTRINGEXACT : LB_FINDSTRINGEXACT;
    UINT set=kind==E_SELECT ? CB_SETCURSEL : LB_SETCURSEL;
    LRESULT idx=text[0] ? SendMessageA(c,find,(WPARAM)-1,(LPARAM)text) : -1;
    SendMessageA(c,set,idx,0);
}

// tiap window punya thread + message loop sendiri, jadi script tetap jalan selama window terbuka
static DWORD WINAPI windowThread(LPVOID param){
    int slot=(int)(INT_PTR)param;
    NrxlWin *win=&wins[slot];
    // panjang x lebar = ukuran area isi window (tanpa title bar & border)
    RECT r={0,0,win->w,win->h};
    AdjustWindowRect(&r,WS_OVERLAPPEDWINDOW,FALSE);
    HWND hwnd=CreateWindowExA(0,"NrxlWindow",win->title,WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,
        NULL,NULL,GetModuleHandleA(NULL),NULL);
    Template *t=win->tpl>=0 ? &templates[win->tpl] : NULL;
    if(hwnd){
        SetWindowLongPtrA(hwnd,GWLP_USERDATA,slot);
        for(int i=0; t && i<t->count; i++){
            int kind=t->el[i].kind;
            const char *cls="STATIC";
            const char *txt=win->shown[i];
            DWORD style=WS_CHILD|WS_VISIBLE, exStyle=0;
            int w=10, h=10;
            if(kind==E_BUTTON){
                cls="BUTTON";
                style|=(win->ownerDraw[i] ? BS_OWNERDRAW : BS_PUSHBUTTON)|WS_TABSTOP;
            } else if(kind==E_INPUT){
                cls="EDIT";
                style|=ES_AUTOHSCROLL|WS_TABSTOP;
                exStyle=WS_EX_CLIENTEDGE;
            } else if(kind==E_CHECK){
                cls="BUTTON";
                style|=BS_AUTOCHECKBOX|WS_TABSTOP;
                txt=win->opts[i];
            } else if(kind==E_SELECT){
                cls="COMBOBOX";
                style|=CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP;
                txt="";
            } else if(kind==E_LIST){
                cls="LISTBOX";
                style|=LBS_NOTIFY|WS_VSCROLL|WS_TABSTOP;
                exStyle=WS_EX_CLIENTEDGE;
                txt="";
            } else if(kind==E_CANVAS || kind==E_IMAGE){
                cls="NrxlCanvas";
                txt="";
                w=win->canvas[i]->w;
                h=win->canvas[i]->h;
            } else {
                style|=SS_LEFTNOWORDWRAP|SS_NOPREFIX;
            }
            HWND c=CreateWindowExA(exStyle,cls,txt,style,
                win->ex[i],win->ey[i],w,h,hwnd,(HMENU)(INT_PTR)(CTL_ID+i),GetModuleHandleA(NULL),NULL);
            SendMessageA(c,WM_SETFONT,(WPARAM)win->font[i],TRUE);
            fitControl(c,txt,kind,win->ew[i],win->eh[i]);
            if(kind==E_CANVAS || kind==E_IMAGE){
                SetWindowLongPtrA(c,GWLP_USERDATA,(LONG_PTR)win->canvas[i]);
                win->canvas[i]->hwnd=c;
            }
            if(kind==E_CHECK) SendMessageA(c,BM_SETCHECK,strcmp(win->shown[i],"true")==0 ? BST_CHECKED : BST_UNCHECKED,0);
            if(kind==E_SELECT || kind==E_LIST){
                fillOptions(c,kind,win->opts[i]);
                selectText(c,kind,win->shown[i]);
            }
            if(kind==E_BUTTON && win->ownerDraw[i]){
                win->oldProc[i]=(WNDPROC)SetWindowLongPtrA(c,GWLP_WNDPROC,(LONG_PTR)buttonProc);
            }
            win->ctl[i]=c;
        }
        for(int k=0; t && k<t->ntimers; k++) SetTimer(hwnd,TIMER_ID+k,win->timerMs[k],NULL);
        win->hwnd=hwnd;
    }
    SetEvent(win->ready);
    if(hwnd){
        ShowWindow(hwnd,SW_SHOW);
        UpdateWindow(hwnd);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
        MSG m;
        while(GetMessageA(&m,NULL,0,0)>0){
            // onKey: tombol yang ditekan di window ini masuk ke onKey saja (tidak ikut menggeser
            // dropdown atau menekan tombol yang sedang fokus), kecuali Tab dan saat mengetik di newInput
            if(m.message==WM_KEYDOWN && t && t->keyFunc>=0 && m.wParam!=VK_TAB){
                char cls[16]="";
                HWND f=GetFocus();
                if(f) GetClassNameA(f,cls,sizeof cls);
                if(_stricmp(cls,"Edit")!=0){
                    Event e;
                    memset(&e,0,sizeof e);
                    e.type=EV_KEY;
                    e.fpc=t->keyFunc;
                    e.slot=-1;
                    keyName((int)m.wParam,e.key,sizeof e.key);
                    if(e.key[0]) pushEvent(&e);
                    continue;
                }
            }
            // IsDialogMessage supaya tombol Tab bisa pindah antar input/tombol
            if(IsDialogMessageA(hwnd,&m)) continue;
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
    }
    win->hwnd=NULL;
    InterlockedDecrement(&openWins);
    SetEvent(clickEvent);
    return 0;
}

static int registerWindowClass(){
    static int registered=0;
    if(registered) return 1;
    WNDCLASSA wc={0};
    wc.lpfnWndProc=nrxlWndProc;
    wc.hInstance=GetModuleHandleA(NULL);
    wc.lpszClassName="NrxlWindow";
    wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    if(!loadGdi() || !RegisterClassA(&wc)) return 0;
    WNDCLASSA cc={0};
    cc.lpfnWndProc=canvasProc;
    cc.hInstance=GetModuleHandleA(NULL);
    cc.lpszClassName="NrxlCanvas";
    cc.hCursor=LoadCursor(NULL,IDC_ARROW);
    if(!RegisterClassA(&cc)) return 0;
    NONCLIENTMETRICSA ncm;
    memset(&ncm,0,sizeof ncm);
    ncm.cbSize=sizeof ncm;
    if(SystemParametersInfoA(SPI_GETNONCLIENTMETRICS,sizeof ncm,&ncm,0)){
        baseFont=ncm.lfMessageFont;
        uiFont=pCreateFontIndirectA(&baseFont);
    } else {
        uiFont=(HFONT)pGetStockObject(DEFAULT_GUI_FONT);
        memset(&baseFont,0,sizeof baseFont);
        strcpy(baseFont.lfFaceName,"Segoe UI");
    }
    InitializeCriticalSection(&clickLock);
    clickEvent=CreateEventA(NULL,FALSE,FALSE,NULL);
    registered=1;
    return 1;
}

// teks variabel global saat ini (dibuat kosong kalau belum ada)
static char *globalStr(const char *name){
    Value *g=findGlobalVal(name);
    if(!g){
        setGlobalVar(name,valStr(""));
        g=findGlobalVal(name);
    }
    return valToStr(*g);
}

// nilai bool variabel newCheck sebagai "true"/"false"
static void checkState(const char *name, char *out, int size){
    Value *g=findGlobalVal(name);
    if(!g){
        setGlobalVar(name,valBool(0));
        g=findGlobalVal(name);
    }
    snprintf(out,size,"%s",valTruthy(*g) ? "true" : "false");
}

// list pilihan newSelect/newList -> teks dipisah \n
static void optionsText(const char *src, char *out, int size){
    Value v=evalExpr(src);
    if(v.type!=T_LIST) nrxFail("pilihan harus list, contoh: [\"mudah\", \"sulit\"]");
    out[0]=0;
    for(int i=0;i<v.list->count;i++){
        char *s=valToStr(v.list->items[i]);
        if(i>0) strncat(out,"\n",size-strlen(out)-1);
        strncat(out,s,size-strlen(out)-1);
        free(s);
    }
    valFree(&v);
}

static Canvas *newCanvasBuffer(const char *name, int w, int h, COLORREF bg){
    if(canvasCount>=64) nrxFail("canvas terlalu banyak (maksimal 64)");
    if(w<=0 || h<=0) nrxFail("lebar dan tinggi canvas harus lebih dari 0");
    Canvas *cv=&canvases[canvasCount++];
    memset(cv,0,sizeof *cv);
    snprintf(cv->name,sizeof cv->name,"%s",name);
    cv->w=w;
    cv->h=h;
    cv->bg=bg;
    HDC screen=GetDC(NULL);
    cv->dc=pCreateCompatibleDC(screen);
    cv->bmp=pCreateCompatibleBitmap(screen,w,h);
    ReleaseDC(NULL,screen);
    pSelectObject(cv->dc,cv->bmp);
    InitializeCriticalSection(&cv->lock);
    RECT r={0,0,w,h};
    HBRUSH b=pCreateSolidBrush(bg);
    FillRect(cv->dc,&r,b);
    pDeleteObject(b);
    return cv;
}

static int openWindow(const char *title, int w, int h, int tpl, const int *wstyles, int nws){
    if(!registerWindowClass()) return 0;
    if(winCount>=64) return 0;
    int slot=winCount;
    NrxlWin *win=&wins[slot];
    memset(win,0,sizeof *win);
    snprintf(win->title,sizeof win->title,"%s",title);
    win->w=w;
    win->h=h;
    win->tpl=tpl;
    // style window: latar window, dan bawaan font/warna untuk isinya
    Look wl;
    lookInit(&wl);
    for(int k=0;k<nws;k++) lookApply(&wl,&styles[wstyles[k]]);
    if(wl.hasBg){
        win->hasBg=1;
        win->bg=wl.bg;
        win->bgBrush=pCreateSolidBrush(wl.bg);
    }
    COLORREF windowBg=wl.hasBg ? wl.bg : GetSysColor(COLOR_WINDOW);
    if(tpl>=0){
        Template *t=&templates[tpl];
        for(int k=0;k<t->ntimers;k++){
            Value v=evalExpr(t->timers[k].interval);
            int ms=(int)(valToNum(v)*1000+0.5);
            valFree(&v);
            win->timerMs[k]=ms<10 ? 10 : ms;
        }
        for(int i=0;i<t->count;i++){
            Elem *el=&t->el[i];
            int kind=el->kind;
            win->ex[i]=evalInt(el->x);
            win->ey[i]=evalInt(el->y);
            // urutan: bawaan dari style window -> style elemen -> argumen ukuran/warna/lebar
            Look l;
            lookInit(&l);
            l.size=wl.size;
            snprintf(l.font,sizeof l.font,"%s",wl.font);
            l.bold=wl.bold;
            l.italic=wl.italic;
            if((kind==E_TEXT || kind==E_CHECK) && wl.hasColor){
                l.hasColor=1;
                l.color=wl.color;
            }
            for(int k=0;k<el->nstyles;k++) lookApply(&l,&styles[el->styles[k]]);
            if(el->size[0]) l.size=evalInt(el->size);
            if(el->color[0]){
                l.color=evalColor(el->color);
                l.hasColor=1;
            }
            if(el->width[0]) l.width=evalInt(el->width);
            if(el->height[0]) l.height=evalInt(el->height);

            if(kind==E_INPUT || kind==E_SELECT || kind==E_LIST){
                char *s=globalStr(el->text);
                snprintf(win->shown[i],sizeof win->shown[i],"%s",s);
                free(s);
            } else if(kind==E_CHECK){
                checkState(el->text,win->shown[i],sizeof win->shown[i]);
                char *label=evalSeqStr(el->extra);
                snprintf(win->opts[i],sizeof win->opts[i],"%s",label);
                free(label);
            } else if(kind==E_CANVAS){
                // newCanvas(layar, ...) juga membuat variabel layar berisi nama canvas, untuk drawRect(layar, ...)
                win->canvas[i]=newCanvasBuffer(el->text,l.width,l.height,l.hasBg ? l.bg : RGB(255,255,255));
                setGlobalVar(el->text,valStr(el->text));
            } else if(kind==E_IMAGE){
                char *path=evalSeqStr(el->text);
                void *img;
                UINT iw, ih;
                loadImage(path,&img,&iw,&ih);
                int dw=l.width>0 ? l.width : (int)iw, dh=l.height>0 ? l.height : (int)ih;
                win->canvas[i]=newCanvasBuffer("",dw,dh,l.hasBg ? l.bg : windowBg);
                paintImage(win->canvas[i]->dc,img,0,0,dw,dh);
                free(path);
            } else {
                char *s=evalSeqStr(el->text);
                snprintf(win->shown[i],sizeof win->shown[i],"%s",s);
                free(s);
            }
            if(kind==E_SELECT || kind==E_LIST) optionsText(el->extra,win->opts[i],sizeof win->opts[i]);

            win->font[i]=fontFor(&l);
            win->hasColor[i]=l.hasColor;
            win->color[i]=l.color;
            if(l.hasBg){
                win->hasEbg[i]=1;
                win->ebg[i]=l.bg;
                win->ebgBrush[i]=pCreateSolidBrush(l.bg);
            }
            win->hasHover[i]=l.hasHover;
            win->hover[i]=l.hover;
            win->hasHoverColor[i]=l.hasHoverColor;
            win->hoverColor[i]=l.hoverColor;
            win->ew[i]=l.width;
            win->eh[i]=l.height;
            win->ownerDraw[i]=kind==E_BUTTON && (l.hasColor || l.hasBg || l.hasHover || l.hasHoverColor);
        }
    }
    win->ready=CreateEventA(NULL,TRUE,FALSE,NULL);
    InterlockedIncrement(&openWins);
    HANDLE th=CreateThread(NULL,0,windowThread,(LPVOID)(INT_PTR)slot,0,NULL);
    if(!th){
        InterlockedDecrement(&openWins);
        CloseHandle(win->ready);
        return 0;
    }
    winThreads[winCount++]=th;
    // tunggu sampai window & isinya selesai dibuat
    WaitForSingleObject(win->ready,5000);
    CloseHandle(win->ready);
    return win->hwnd!=NULL;
}

// isi yang diubah user di window (ketikan, centang, pilihan) masuk ke variabelnya
static void syncInputs(){
    for(int s=0;s<winCount;s++){
        NrxlWin *win=&wins[s];
        if(!win->hwnd || win->tpl<0) continue;
        Template *t=&templates[win->tpl];
        for(int i=0;i<t->count;i++){
            HWND c=win->ctl[i];
            int kind=t->el[i].kind;
            if(!c || !IsWindow(c)) continue;
            char buf[256]="";
            Value v;
            if(kind==E_INPUT){
                GetWindowTextA(c,buf,sizeof buf);
                if(strcmp(buf,win->shown[i])==0) continue;
                v=valStr(buf);
            } else if(kind==E_CHECK){
                int on=SendMessageA(c,BM_GETCHECK,0,0)==BST_CHECKED;
                snprintf(buf,sizeof buf,"%s",on ? "true" : "false");
                if(strcmp(buf,win->shown[i])==0) continue;
                v=valBool(on);
            } else if(kind==E_SELECT || kind==E_LIST){
                UINT getSel=kind==E_SELECT ? CB_GETCURSEL : LB_GETCURSEL;
                UINT getLen=kind==E_SELECT ? CB_GETLBTEXTLEN : LB_GETTEXTLEN;
                UINT getText=kind==E_SELECT ? CB_GETLBTEXT : LB_GETTEXT;
                LRESULT idx=SendMessageA(c,getSel,0,0);
                if(idx>=0 && SendMessageA(c,getLen,idx,0)<(LRESULT)sizeof buf) SendMessageA(c,getText,idx,(LPARAM)buf);
                if(strcmp(buf,win->shown[i])==0) continue;
                v=valStr(buf);
            } else {
                continue;
            }
            strcpy(win->shown[i],buf);
            setGlobalVar(t->el[i].text,v);
        }
    }
}

// perbarui isi window yang berubah karena variabelnya diubah script, dan tampilkan gambar canvas terbaru
static void refreshWindows(){
    syncInputs();
    for(int s=0;s<winCount;s++){
        NrxlWin *win=&wins[s];
        if(!win->hwnd || win->tpl<0) continue;
        Template *t=&templates[win->tpl];
        for(int i=0;i<t->count;i++){
            Elem *el=&t->el[i];
            HWND c=win->ctl[i];
            if(!c || !IsWindow(c)) continue;
            int kind=el->kind;
            if(kind==E_CANVAS){
                if(win->canvas[i]->dirty){
                    win->canvas[i]->dirty=0;
                    InvalidateRect(c,NULL,FALSE);
                }
                continue;
            }
            if(kind==E_IMAGE) continue;
            if(kind==E_SELECT || kind==E_LIST){
                char opts[1024];
                optionsText(el->extra,opts,sizeof opts);
                if(strcmp(opts,win->opts[i])!=0){
                    strcpy(win->opts[i],opts);
                    fillOptions(c,kind,opts);
                    selectText(c,kind,win->shown[i]);
                }
            }
            char now[256];
            if(kind==E_CHECK){
                checkState(el->text,now,sizeof now);
            } else {
                char *s=(kind==E_INPUT || kind==E_SELECT || kind==E_LIST) ? globalStr(el->text) : evalSeqStr(el->text);
                snprintf(now,sizeof now,"%s",s);
                free(s);
            }
            if(strcmp(now,win->shown[i])==0) continue;
            snprintf(win->shown[i],sizeof win->shown[i],"%s",now);
            if(kind==E_CHECK){
                SendMessageA(c,BM_SETCHECK,strcmp(now,"true")==0 ? BST_CHECKED : BST_UNCHECKED,0);
            } else if(kind==E_SELECT || kind==E_LIST){
                selectText(c,kind,now);
            } else {
                // ubah ukuran dulu baru ganti teks, lalu gambar ulang semuanya
                // (kalau terbalik, sisa gambar teks lama bisa memotong huruf)
                if(kind!=E_INPUT) fitControl(c,now,kind,win->ew[i],win->eh[i]);
                SetWindowTextA(c,now);
                InvalidateRect(c,NULL,TRUE);
            }
        }
    }
}

// jalankan event (klik, tombol, timer) yang sudah antri
static void processClicks(){
    static int busy=0;
    if(busy || !clickEvent) return;
    busy=1;
    Event e;
    while(popEvent(&e)){
        syncInputs();
        // argumen dikirim sebanyak parameter fungsinya:
        // onKey -> (tombol), onMouse -> (x, y, aksi, canvas), klik dan timer -> ()
        Value args[4];
        int n=0;
        if(e.type==EV_KEY){
            args[n++]=valStr(e.key);
        } else if(e.type==EV_MOUSE){
            args[n++]=valNum(e.mx);
            args[n++]=valNum(e.my);
            args[n++]=valStr(e.action);
            args[n++]=valStr(e.canvas);
        }
        int want=funcParamCount(e.fpc);
        int give=want<n ? want : n;
        Value r=callFunc(e.fpc,give ? args : NULL,give);
        valFree(&r);
        for(int k=0;k<n;k++) valFree(&args[k]);
        if(e.type==EV_TIMER) wins[e.slot].timerPending[e.timer]=0;
        if(e.type==EV_MOUSE && !strcmp(e.action,"move")) wins[e.slot].movePending=0;
        refreshWindows();
    }
    busy=0;
}

// ---------------- canvas ----------------

static Canvas *needCanvas(const char *fn, Value v){
    char *name=valToStr(v);
    for(int i=canvasCount-1;i>=0;i--){
        if(canvases[i].name[0] && !strcmp(canvases[i].name,name)){
            free(name);
            return &canvases[i];
        }
    }
    nrxFail("%s: canvas %s tidak ditemukan (buat dulu dengan newCanvas di windowTemplate, lalu buka window-nya)",fn,name);
    return NULL;
}

static int argInt(const char *fn, Value v){
    double d;
    if(!numLike(v,&d)) nrxFail("%s butuh angka untuk posisi dan ukuran",fn);
    return (int)d;
}

static COLORREF argColor(Value v){
    char *s=valToStr(v);
    COLORREF c=colorOf(s);
    free(s);
    return c;
}
#endif

static const char *drawFuncs[]={"clearCanvas","drawRect","drawCircle","drawLine","drawText","drawImage"};

int nrxWindowFuncOwns(const char *name){
    for(size_t i=0;i<sizeof drawFuncs/sizeof drawFuncs[0];i++){
        if(!strcmp(name,drawFuncs[i])) return 1;
    }
    return 0;
}

Value nrxWindowCall(const char *name, Value *a, int n){
    #ifdef _WIN32
    // semua argumen dicek dulu sebelum canvas dikunci, supaya error (yang bisa ditangkap try)
    // tidak meninggalkan canvas dalam keadaan terkunci
    if(n<1) nrxFail("%s butuh nama canvas sebagai argumen pertama",name);
    Canvas *cv=needCanvas(name,a[0]);
    HDC dc=cv->dc;
    if(!strcmp(name,"clearCanvas")){
        // clearCanvas(canvas) atau clearCanvas(canvas, warna)
        if(n>2) nrxFail("cara menulis: clearCanvas(canvas, warna)");
        COLORREF c=n==2 ? argColor(a[1]) : cv->bg;
        RECT r={0,0,cv->w,cv->h};
        HBRUSH b=pCreateSolidBrush(c);
        EnterCriticalSection(&cv->lock);
        FillRect(dc,&r,b);
        LeaveCriticalSection(&cv->lock);
        pDeleteObject(b);
    } else if(!strcmp(name,"drawRect")){
        if(n!=6) nrxFail("cara menulis: drawRect(canvas, x, y, lebar, tinggi, warna)");
        int x=argInt(name,a[1]), y=argInt(name,a[2]);
        RECT r={x,y,x+argInt(name,a[3]),y+argInt(name,a[4])};
        HBRUSH b=pCreateSolidBrush(argColor(a[5]));
        EnterCriticalSection(&cv->lock);
        FillRect(dc,&r,b);
        LeaveCriticalSection(&cv->lock);
        pDeleteObject(b);
    } else if(!strcmp(name,"drawCircle")){
        // x, y = titik tengah lingkaran
        if(n!=5) nrxFail("cara menulis: drawCircle(canvas, x, y, jariJari, warna)");
        int x=argInt(name,a[1]), y=argInt(name,a[2]), rad=argInt(name,a[3]);
        HBRUSH b=pCreateSolidBrush(argColor(a[4]));
        EnterCriticalSection(&cv->lock);
        HGDIOBJ ob=pSelectObject(dc,b);
        HGDIOBJ op=pSelectObject(dc,pGetStockObject(NULL_PEN));
        pEllipse(dc,x-rad,y-rad,x+rad+1,y+rad+1);
        pSelectObject(dc,op);
        pSelectObject(dc,ob);
        LeaveCriticalSection(&cv->lock);
        pDeleteObject(b);
    } else if(!strcmp(name,"drawLine")){
        if(n!=6 && n!=7) nrxFail("cara menulis: drawLine(canvas, x1, y1, x2, y2, warna, tebal)");
        int x1=argInt(name,a[1]), y1=argInt(name,a[2]), x2=argInt(name,a[3]), y2=argInt(name,a[4]);
        int tebal=n==7 ? argInt(name,a[6]) : 1;
        HPEN p=pCreatePen(PS_SOLID,tebal<1 ? 1 : tebal,argColor(a[5]));
        EnterCriticalSection(&cv->lock);
        HGDIOBJ op=pSelectObject(dc,p);
        pMoveToEx(dc,x1,y1,NULL);
        pLineTo(dc,x2,y2);
        pSelectObject(dc,op);
        LeaveCriticalSection(&cv->lock);
        pDeleteObject(p);
    } else if(!strcmp(name,"drawText")){
        if(n!=5 && n!=6) nrxFail("cara menulis: drawText(canvas, teks, x, y, warna, ukuranHuruf)");
        Look l;
        lookInit(&l);
        l.size=n==6 ? argInt(name,a[5]) : 16;
        int x=argInt(name,a[2]), y=argInt(name,a[3]);
        COLORREF c=argColor(a[4]);
        HFONT f=fontFor(&l);
        char *s=valToStr(a[1]);
        EnterCriticalSection(&cv->lock);
        HGDIOBJ of=pSelectObject(dc,f);
        pSetBkMode(dc,TRANSPARENT);
        pSetTextColor(dc,c);
        pTextOutA(dc,x,y,s,strlen(s));
        pSelectObject(dc,of);
        LeaveCriticalSection(&cv->lock);
        free(s);
    } else {
        // drawImage(canvas, "file.png", x, y) atau dengan lebar, tinggi
        if(n!=4 && n!=6) nrxFail("cara menulis: drawImage(canvas, \"file.png\", x, y, lebar, tinggi)");
        char *path=valToStr(a[1]);
        void *img;
        UINT iw, ih;
        loadImage(path,&img,&iw,&ih);
        free(path);
        int x=argInt(name,a[2]), y=argInt(name,a[3]);
        int w=n==6 ? argInt(name,a[4]) : (int)iw, h=n==6 ? argInt(name,a[5]) : (int)ih;
        EnterCriticalSection(&cv->lock);
        paintImage(dc,img,x,y,w,h);
        LeaveCriticalSection(&cv->lock);
    }
    cv->dirty=1;
    return valStr("");
    #else
    (void)a;
    (void)n;
    nrxFail("%s hanya didukung di Windows",name);
    return valStr("");
    #endif
}

// windowTemplate/templateStyle: blok yang sudah dibaca saat prepass
int nrxWindowIsTemplate(int pc){
    return startsWord(lines[pc],"windowTemplate") || startsWord(lines[pc],"templateStyle");
}

int nrxWindowNext(int pc){
    return templateEnd(pc)+1;
}

int nrxWindowExec(int pc){
    char *text=lines[pc];
    if(startsWord(text,"windowTemplate") || startsWord(text,"templateStyle")){
        // sudah dibaca saat prepass, cukup dilewati
        return templateEnd(pc)+1;
    }
    if(startsWord(text,"closeWindow")){
        // closeWindow() menutup semua window, closeWindow(nama) menutup window dengan judul itu
        char a[2][256];
        int na=splitArgs(text,a,2);
        if(na>1) nrxFail("closeWindow butuh 0 atau 1 argumen: closeWindow(nama)");
        #ifdef _WIN32
        char *title=na==1 ? evalSeqStr(a[0]) : NULL;
        for(int s=0;s<winCount;s++){
            if(wins[s].hwnd && (!title || strcmp(wins[s].title,title)==0)) PostMessageA(wins[s].hwnd,WM_CLOSE,0,0);
        }
        free(title);
        #endif
        return pc+1;
    }
    // newWindow(nama, panjang, lebar, templateName, style...)
    char a[8][256];
    int na=splitArgs(text,a,8);
    if(na<3 || na>8) nrxFail("newWindow butuh minimal 3 argumen: newWindow(nama, panjang, lebar, templateName, style)");
    int w=evalInt(a[1]);
    int h=evalInt(a[2]);
    if(w<=0 || h<=0) nrxFail("panjang dan lebar window harus lebih dari 0");
    int tpl=-1;
    int wstyles[5], nws=0;
    for(int k=3;k<na;k++){
        if(findTemplate(a[k])>=0){
            if(tpl>=0) nrxFail("newWindow hanya boleh memakai 1 windowTemplate");
            tpl=findTemplate(a[k]);
        } else if(findStyle(a[k])>=0){
            wstyles[nws++]=findStyle(a[k]);
        } else {
            nrxFail("%s bukan windowTemplate atau templateStyle",a[k]);
        }
    }
    char *title=evalSeqStr(a[0]);
    #ifdef _WIN32
    if(!openWindow(title,w,h,tpl,wstyles,nws)) nrxFail("gagal membuka window");
    #else
    (void)wstyles;
    nrxFail("newWindow hanya didukung di Windows");
    #endif
    free(title);
    return pc+1;
}

void nrxWindowWait(double sec){
    #ifdef _WIN32
    if(!clickEvent){
        Sleep((DWORD)(sec*1000+0.5));
        return;
    }
    refreshWindows();
    DWORD total=(DWORD)(sec*1000+0.5);
    DWORD start=GetTickCount();
    for(;;){
        DWORD elapsed=GetTickCount()-start;
        if(elapsed>=total) break;
        if(WaitForSingleObject(clickEvent,total-elapsed)==WAIT_OBJECT_0) processClicks();
    }
    #else
    usleep((useconds_t)(sec*1000000));
    #endif
}

// program baru selesai setelah semua window ditutup; sambil menunggu, klik tombol tetap dilayani
void nrxWindowFinish(void){
    #ifdef _WIN32
    if(clickEvent) refreshWindows();
    while(openWins>0){
        WaitForSingleObject(clickEvent,INFINITE);
        processClicks();
    }
    for(int i=0;i<winCount;i++){
        WaitForSingleObject(winThreads[i],INFINITE);
        CloseHandle(winThreads[i]);
    }
    #endif
}
