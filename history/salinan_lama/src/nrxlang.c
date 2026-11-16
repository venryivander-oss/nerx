#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <setjmp.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "nrxlang.h"
#include "nrxrt.h"
#include "nrx_window.h"
#include "nrx_rand.h"
#include "nrx_crudfile.h"
#include "nrx_keyboard.h"
#include "nrx_console.h"
#include "nrx_sound.h"
#include "nrx_3d.h"
#include "nrx_http.h"
#include "nrx_json.h"

char lines[MAX_LINES][512];
int indents[MAX_LINES];
int lineNo[MAX_LINES];
int lineCount=0;
char *srcFile;
int curPc=-1;   // baris yang sedang jalan; juga diisi kode hasil compile (nrxrt.h)

// file asal tiap baris (file utama + file yang di-im)
static char *files[64];
static int fileCount=0;
static int lineFile[MAX_LINES];

// try/catch: nrxFail di dalam try melompat ke catch, bukan menghentikan program
static jmp_buf tryBufs[64];
static int tryTop=0;
static char tryMsg[1024];

// library yang dipakai script lewat: lib <nama>
Value (*nrx3dCallPtr)(const char *name, Value *args, int nargs)=NULL;
static int windowLib=0, randLib=0, fileLib=0, keyboardLib=0, consoleLib=0, soundLib=0, jsonLib=0, threeDLib=0, httpLib=0;

// ---------------- error ----------------

#include "nrx_msgs.h"

// bahasa pesan error: 0 = indonesia, 1 = inggris. Mengikuti bahasa Windows,
// bisa diganti dengan baris "errlang = en" di file utama
static int errLang=1;

// terjemahan pesan; *code diisi nomor E#### (0 kalau pesan tidak ada di tabel)
static const char *trMsg(const char *id, int *code){
    for(size_t i=0;i<sizeof errMsgs/sizeof errMsgs[0];i++){
        if(strcmp(errMsgs[i][0],id)==0){
            if(code) *code=(int)i+1;
            return errMsgs[i][errLang];
        }
    }
    if(code) *code=0;
    return id;
}

const char *nrxTr(const char *msg){
    return trMsg(msg,NULL);
}

static void printError(const char *file, int line, int code, const char *msg){
    fflush(stdout);
    printf("error");
    if(code) printf(" E%04d",code);
    if(file){
        printf(errLang ? " in %s" : " di %s",file);
        if(line>0) printf(":%d",line);
    }
    printf(": %s\n",msg);
}

static void vfail(int pc, const char *fmt, va_list ap){
    int code;
    vsnprintf(tryMsg,sizeof tryMsg,trMsg(fmt,&code),ap);
    if(tryTop>0) longjmp(tryBufs[tryTop-1],1);
    if(pc>=0 && pc<lineCount) printError(files[lineFile[pc]],lineNo[pc],code,tryMsg);
    else printError(srcFile,0,code,tryMsg);
    exit(1);
}

// error saat file masih dibaca (sebelum lines[] siap)
static void loadFail(const char *file, int line, const char *fmt, ...){
    char msg[1024];
    int code;
    va_list ap;
    va_start(ap,fmt);
    vsnprintf(msg,sizeof msg,trMsg(fmt,&code),ap);
    va_end(ap);
    printError(file,line,code,msg);
    exit(1);
}

// pembaca file sumber: dari disk, atau dari teks yang ditanam di .exe hasil compile
// (nrxlang build). Kalau ada teks tertanam, disk tidak disentuh sama sekali.
static const char **embedNames, **embedTexts;
static int embedCount=0;
typedef struct { FILE *f; const char *p; } SrcReader;

static int srcOpen(SrcReader *r, const char *path){
    r->f=NULL;
    r->p=NULL;
    for(int i=0;i<embedCount;i++){
        if(!strcmp(embedNames[i],path)){
            r->p=embedTexts[i];
            return 1;
        }
    }
    if(embedCount) return 0;
    r->f=fopen(path,"r");
    return r->f!=NULL;
}

static char *srcGets(char *buf, int size, SrcReader *r){
    if(r->f) return fgets(buf,size,r->f);
    if(!*r->p) return NULL;
    int k=0;
    while(*r->p && k<size-1){
        char c=*r->p++;
        buf[k++]=c;
        if(c=='\n') break;
    }
    buf[k]=0;
    return buf;
}

static void srcClose(SrcReader *r){
    if(r->f) fclose(r->f);
}

// errlang ikut bahasa Windows; baris "errlang = id/en" di file utama mengganti pilihan itu
static void detectErrLang(const char *path){
    #ifdef _WIN32
    errLang=PRIMARYLANGID(GetUserDefaultUILanguage())!=LANG_INDONESIAN;
    #else
    const char *l=getenv("LANG");
    errLang=!(l && strncmp(l,"id",2)==0);
    #endif
    SrcReader f;
    if(!srcOpen(&f,path)) return;
    char line[512];
    int n=0;
    while(srcGets(line,sizeof line,&f)){
        n++;
        line[strcspn(line,"\r\n#")]=0;
        if(!startsWord(line,"errlang")) continue;
        char *p=line+7;
        while(*p==' ' || *p=='\t') p++;
        if(*p=='=') p++;
        while(*p==' ' || *p=='\t') p++;
        char val[16];
        snprintf(val,sizeof val,"%s",p);
        trim(val);
        if(!strcmp(val,"id")) errLang=0;
        else if(!strcmp(val,"en")) errLang=1;
        else {
            srcClose(&f);
            loadFail(path,n,"cara menulis: errlang = id  atau  errlang = en");
        }
    }
    srcClose(&f);
}

void nrxFail(const char *fmt, ...){
    va_list ap;
    va_start(ap,fmt);
    vfail(curPc,fmt,ap);
    va_end(ap);
}

void nrxFailAt(int pc, const char *fmt, ...){
    va_list ap;
    va_start(ap,fmt);
    vfail(pc,fmt,ap);
    va_end(ap);
}

// ---------------- nilai ----------------

Value valNum(double d){
    Value v={T_NUM,d,NULL,NULL};
    v.sub=d!=floor(d);
    return v;
}

Value valDec(double d){
    Value v={T_NUM,d,NULL,NULL};
    v.sub=1;
    return v;
}

Value valStrOwn(char *s){
    Value v={T_STR,0,s,NULL};
    return v;
}

Value valStr(const char *s){
    return valStrOwn(strdup(s?s:""));
}

Value valBool(int b){
    Value v=valStr(b?"true":"false");
    v.sub=1;
    return v;
}

Value valList(List *l){
    Value v={T_LIST,0,NULL,l};
    return v;
}

Value valArr(NumArr *a){
    Value v={T_ARR,0,NULL,NULL,NULL,0,a};
    return v;
}

// array berisi n angka 0.0
NumArr *arrNew(int n){
    NumArr *a=calloc(1,sizeof(NumArr));
    a->cap=n>0 ? n : 4;
    a->d=calloc(a->cap,sizeof(double));
    a->count=n;
    a->refs=1;
    return a;
}

static void arrPush(NumArr *a, double d){
    if(a->count==a->cap){
        a->cap*=2;
        a->d=realloc(a->d,a->cap*sizeof(double));
    }
    a->d[a->count++]=d;
}

// ---- memori: reference counting ----
// list dan map dipegang bersama (be b = a: b dan a memegang list yang sama). Tiap
// pemegang (variabel, isi list/map, argumen) menambah refs lewat valCopy dan
// menguranginya lewat valFree; saat refs jadi 0 isinya dibebaskan. Teks selalu disalin.
// Build dengan -DNRX_RCDEBUG: memori tidak benar-benar dibebaskan, tapi ditandai mati,
// dan pemakaian setelah bebas langsung dilaporkan (dipakai run_tests rc).
#ifdef NRX_RCDEBUG
static void rcBug(const char *what){
    fprintf(stderr,"BUG memori: %s (baris %d)\n",what,curPc>=0 && curPc<lineCount ? lineNo[curPc] : 0);
    exit(99);
}
#define RC_ALIVE(x,what) do{ if((x)->refs<=0) rcBug(what); }while(0)
#else
#define RC_ALIVE(x,what) ((void)0)
#endif

static void listRelease(List *l){
    RC_ALIVE(l,"list dibebaskan dua kali");
    if(--l->refs>0) return;
    for(int i=0;i<l->count;i++) valFree(&l->items[i]);
#ifndef NRX_RCDEBUG
    free(l->items);
    free(l);
#endif
}

static void arrRelease(NumArr *a){
    RC_ALIVE(a,"array dibebaskan dua kali");
    if(--a->refs>0) return;
#ifndef NRX_RCDEBUG
    free(a->d);
    free(a);
#endif
}

static void mapRelease(Map *m){
    RC_ALIVE(m,"map dibebaskan dua kali");
    if(--m->refs>0) return;
    for(int i=0;i<m->count;i++){
        free(m->keys[i]);
        valFree(&m->vals[i]);
    }
#ifndef NRX_RCDEBUG
    free(m->keys);
    free(m->vals);
    free(m);
#else
    m->count=0;
#endif
}

Value valCopy(Value v){
    if(v.type==T_STR){
        Value c=valStr(v.str);
        c.sub=v.sub;
        return c;
    }
    if(v.type==T_LIST){
        RC_ALIVE(v.list,"list dipakai setelah dibebaskan");
        v.list->refs++;
    } else if(v.type==T_MAP){
        RC_ALIVE(v.map,"map dipakai setelah dibebaskan");
        v.map->refs++;
    } else if(v.type==T_ARR){
        RC_ALIVE(v.arr,"array dipakai setelah dibebaskan");
        v.arr->refs++;
    }
    return v;
}

void valFree(Value *v){
    if(v->type==T_STR){
        free(v->str);
        v->str=NULL;
    } else if(v->type==T_LIST && v->list){
        listRelease(v->list);
        v->list=NULL;
    } else if(v->type==T_MAP && v->map){
        mapRelease(v->map);
        v->map=NULL;
    } else if(v->type==T_ARR && v->arr){
        arrRelease(v->arr);
        v->arr=NULL;
    }
}

int isNumber(const char *s, double *out){
    while(*s==' '||*s=='\t') s++;
    if(!*s) return 0;
    char *end;
    double d=strtod(s,&end);
    if(end==s) return 0;
    while(*end==' '||*end=='\t') end++;
    if(*end) return 0;
    if(out) *out=d;
    return 1;
}

// int: 5. desimal: 2.5, dan 2.0 untuk desimal yang nilainya bulat supaya jenisnya terlihat
static void fmtNum(double d, int dec, char *b, int size){
    if(d==floor(d) && fabs(d)<1e15) snprintf(b,size,dec ? "%.1f" : "%.0f",d);
    else snprintf(b,size,"%.10g",d);
}

// tambahkan s di akhir buffer out yang bisa membesar
static void sbAdd(char **out, size_t *len, size_t *cap, const char *s){
    size_t sl=strlen(s);
    if(*len+sl+1>*cap){
        while(*len+sl+1>*cap) *cap*=2;
        *out=realloc(*out,*cap);
    }
    memcpy(*out+*len,s,sl+1);
    *len+=sl;
}

char *valToStr(Value v){
    if(v.type==T_STR) return strdup(v.str);
    if(v.type==T_NUM){
        char b[64];
        fmtNum(v.num,v.sub,b,sizeof b);
        return strdup(b);
    }
    // list -> [a, b, c]   map -> {nama: Budi, umur: 20}
    size_t cap=64, len=0;
    char *out=malloc(cap);
    out[0]=0;
    if(v.type==T_ARR){
        sbAdd(&out,&len,&cap,"[");
        for(int i=0;i<v.arr->count;i++){
            char b[64];
            if(i>0) sbAdd(&out,&len,&cap,", ");
            fmtNum(v.arr->d[i],1,b,sizeof b);
            sbAdd(&out,&len,&cap,b);
        }
        sbAdd(&out,&len,&cap,"]");
        return out;
    }
    int isMap=v.type==T_MAP;
    int count=isMap ? v.map->count : v.list->count;
    // struct -> Player{nama: Budi, hp: 100}
    if(isMap && v.map->structName) sbAdd(&out,&len,&cap,v.map->structName);
    sbAdd(&out,&len,&cap,isMap ? "{" : "[");
    for(int i=0;i<count;i++){
        if(i>0) sbAdd(&out,&len,&cap,", ");
        if(isMap){
            sbAdd(&out,&len,&cap,v.map->keys[i]);
            sbAdd(&out,&len,&cap,": ");
        }
        char *s=valToStr(isMap ? v.map->vals[i] : v.list->items[i]);
        sbAdd(&out,&len,&cap,s);
        free(s);
    }
    sbAdd(&out,&len,&cap,isMap ? "}" : "]");
    return out;
}

// angka, atau teks yang isinya angka (mis. hasil ask / newInput)
int numLike(Value v, double *out){
    if(v.type==T_NUM){
        if(out) *out=v.num;
        return 1;
    }
    if(v.type==T_STR) return isNumber(v.str,out);
    return 0;
}

double valToNum(Value v){
    double d;
    return numLike(v,&d) ? d : 0;
}

int valTruthy(Value v){
    if(v.type==T_NUM) return v.num!=0;
    if(v.type==T_LIST) return v.list->count>0;
    if(v.type==T_MAP) return v.map->count>0;
    if(v.type==T_ARR) return v.arr->count>0;
    if(!v.str[0] || strcmp(v.str,"false")==0) return 0;
    double d;
    if(isNumber(v.str,&d)) return d!=0;
    return 1;
}

// ---------------- tipe ketat ----------------

int valKind(Value v){
    if(v.type==T_NUM) return v.sub ? K_DOUBLE : K_INT;
    if(v.type==T_LIST) return K_LIST;
    if(v.type==T_ARR) return K_ARRAY;
    if(v.type==T_MAP) return v.map->structName ? K_STRUCT : K_MAP;
    return v.sub ? K_BOOL : K_STRING;
}

static const char *kindName(int k){
    static const char *names[]={"apa saja","int","double","float","bool","string","list","map","struct","json","array"};
    return names[k];
}

// nama jenis sebuah nilai untuk pesan error; struct memakai namanya
static int listElem(List *l);
static int mapElem(Map *m);

// list dan map ikut menyebut jenis isinya: "list string"
static const char *valKindName(Value v){
    static char buf[2][48];
    static int turn=0;
    if(v.type==T_MAP && v.map->structName) return v.map->structName;
    int e=v.type==T_LIST ? listElem(v.list) : v.type==T_MAP ? mapElem(v.map) : K_ANY;
    if(e==K_ANY || e==K_MIXED) return kindName(valKind(v));
    turn=!turn;
    snprintf(buf[turn],sizeof buf[turn],"%s %s",kindName(valKind(v)),kindName(e));
    return buf[turn];
}

static int kindFromName(const char *t){
    static const char *names[]={"int","double","float","bool","string","list","map","json","array"};
    static const int kinds[]={K_INT,K_DOUBLE,K_FLOAT,K_BOOL,K_STRING,K_LIST,K_MAP,K_MIXED,K_ARRAY};
    for(int i=0;i<9;i++) if(!strcmp(t,names[i])) return kinds[i];
    return K_ANY;
}

static int isNumKind(int k){
    return k==K_INT || k==K_DOUBLE || k==K_FLOAT;
}

// jenis isi list/map; kalau belum diketahui, ditebak dari isi pertama
static int listElem(List *l){
    if(l->elem==K_ANY && l->count>0){
        l->elem=valKind(l->items[0]);
        if(l->elem==K_STRUCT) l->ename=l->items[0].map->structName;
    }
    return l->elem;
}

static int mapElem(Map *m){
    if(m->elem==K_ANY && m->count>0 && !m->structName) m->elem=valKind(m->vals[0]);
    return m->elem;
}

// cek nilai v boleh disimpan di tempat berjenis kind (sname: nama struct, elem: jenis isi
// list/map). int masuk ke double/float otomatis jadi desimal. Hasil: 0 boleh,
// 1 desimal ke int, 2 jenisnya beda
static int fitKind(int kind, const char *sname, int elem, Value *v){
    if(kind==K_ANY || kind==K_MIXED) return 0;
    int vk=valKind(*v);
    if(kind==K_INT) return vk==K_INT ? 0 : vk==K_DOUBLE ? 1 : 2;
    if(kind==K_DOUBLE || kind==K_FLOAT){
        if(!isNumKind(vk)) return 2;
        v->sub=1;
        return 0;
    }
    if(kind==K_STRUCT) return vk==K_STRUCT && (!sname || !strcmp(v->map->structName,sname)) ? 0 : 2;
    if(kind!=vk) return 2;
    if(elem==K_ANY || elem==K_MIXED) return 0;
    if(kind==K_LIST){
        List *l=v->list;
        if(listElem(l)==K_ANY) l->elem=elem;
        if(l->elem==K_MIXED || l->elem==elem) return 0;
        if((elem==K_DOUBLE || elem==K_FLOAT) && l->elem==K_INT){
            for(int i=0;i<l->count;i++) l->items[i].sub=1;
            l->elem=elem;
            return 0;
        }
        return 2;
    }
    if(kind==K_MAP){
        Map *m=v->map;
        if(mapElem(m)==K_ANY) m->elem=elem;
        return m->elem==K_MIXED || m->elem==elem ? 0 : 2;
    }
    return 0;
}

// simpan v ke dalam list/map: jenisnya harus sama dengan isi yang lain
static void fitElem(int *elem, const char **ename, Value *v, const char *where){
    if(*elem==K_MIXED) return;
    if(*elem==K_ANY){
        *elem=valKind(*v);
        if(*elem==K_STRUCT) *ename=v->map->structName;
        return;
    }
    // list Player hanya menerima Player
    if(*elem==K_STRUCT && valKind(*v)==K_STRUCT && !*ename) *ename=v->map->structName;
    int r=fitKind(*elem,*ename,K_ANY,v);
    if(r==2 && *elem==K_STRUCT && *ename) nrxFail("isi %s bertipe %s, tidak bisa diisi %s",where,*ename,valKindName(*v));
    if(r==1) nrxFail("isi %s bertipe int, tidak bisa diisi angka desimal; pakai int(...)",where);
    if(r==2) nrxFail("isi %s bertipe %s, tidak bisa diisi %s",where,kindName(*elem),valKindName(*v));
}

// isi [ ... ] / { ... } yang ditulis langsung harus satu jenis; int dan desimal jadi desimal
static void sameKind(int *elem, const char **ename, Value *v, const char *what){
    int vk=valKind(*v);
    if(vk==K_STRUCT){
        if(*elem==K_ANY) *ename=v->map->structName;
        else if(*elem==K_STRUCT && strcmp(*ename,v->map->structName)) nrxFail("isi list harus satu jenis, tapi ada %s dan %s",*ename,v->map->structName);
    }
    if(*elem==K_ANY || *elem==vk) *elem=vk;
    else if(isNumKind(*elem) && isNumKind(vk)) *elem=K_DOUBLE;
    else if(!strcmp(what,"map")) nrxFail("isi map harus satu jenis, tapi ada %s dan %s; untuk data campur pakai type",kindName(*elem),valKindName(*v));
    else nrxFail("isi list harus satu jenis, tapi ada %s dan %s",kindName(*elem),valKindName(*v));
}

List *listNew(void){
    List *l=calloc(1,sizeof(List));
    l->refs=1;
    return l;
}

void listPush(List *l, Value v){
    RC_ALIVE(l,"list dipakai setelah dibebaskan");
    if(l->count==l->cap){
        l->cap=l->cap ? l->cap*2 : 8;
        l->items=realloc(l->items,l->cap*sizeof(Value));
    }
    l->items[l->count++]=v;
}

Value valMap(Map *m){
    Value v={T_MAP,0,NULL,NULL,m};
    return v;
}

Map *mapNew(void){
    Map *m=calloc(1,sizeof(Map));
    m->refs=1;
    return m;
}

static int mapIndex(Map *m, const char *key){
    for(int i=0;i<m->count;i++){
        if(strcmp(m->keys[i],key)==0) return i;
    }
    return -1;
}

Value *mapGet(Map *m, const char *key){
    RC_ALIVE(m,"map dipakai setelah dibebaskan");
    int i=mapIndex(m,key);
    return i>=0 ? &m->vals[i] : NULL;
}

void mapSet(Map *m, const char *key, Value v){
    RC_ALIVE(m,"map dipakai setelah dibebaskan");
    int i=mapIndex(m,key);
    if(i>=0){
        valFree(&m->vals[i]);
        m->vals[i]=v;
        return;
    }
    if(m->count==m->cap){
        m->cap=m->cap ? m->cap*2 : 8;
        m->keys=realloc(m->keys,m->cap*sizeof(char*));
        m->vals=realloc(m->vals,m->cap*sizeof(Value));
    }
    m->keys[m->count]=strdup(key);
    m->vals[m->count]=v;
    m->count++;
}

// hapus key dari map; return nilainya (pemanggil yang membebaskan)
static Value mapRemove(Map *m, const char *key){
    int i=mapIndex(m,key);
    if(i<0) nrxFail("key \"%s\" tidak ada di map",key);
    Value r=m->vals[i];
    free(m->keys[i]);
    memmove(&m->keys[i],&m->keys[i+1],(m->count-i-1)*sizeof(char*));
    memmove(&m->vals[i],&m->vals[i+1],(m->count-i-1)*sizeof(Value));
    m->count--;
    return r;
}

// ---------------- variabel ----------------

typedef struct {
    char name[64];
    Value val;
    int owner;      // file pemilik variabel global (0 = file utama, -1 = dibuat library)
    int exported;   // ditulis dengan ex be / ex lock
    int locked;     // dibuat dengan lock: tidak boleh diubah
    int kind;       // jenis variabel (K_...), tetap sejak dibuat; K_ANY = belum ditentukan
    int elem;       // jenis isi kalau list/map
    const char *sname;  // nama struct kalau kind == K_STRUCT
} Var;

// file asal baris yang sedang dijalankan (0 = file utama)
static int curFile(void){
    return curPc>=0 && curPc<lineCount ? lineFile[curPc] : 0;
}

// milik file utama, dibuat library, milik file ini sendiri, atau di-ex: boleh dipakai
// im yang ditulis file a untuk file b: NULL = tidak pernah di-im langsung, "" = im "b"
// (semua yang di-ex), ",luas,kali2," = im luas, kali2 from "b"
static char *imNames[64][64];
static int imLine[64][64];
static char *imText[64][64];   // nama file seperti ditulis di im, untuk pesan error

static int imAllows(int file, int owner, const char *name){
    const char *list=imNames[file][owner];
    if(!list || !list[0]) return 1;
    char key[80];
    snprintf(key,sizeof key,",%s,",name);
    return strstr(list,key)!=NULL;
}

static int isVisible(int owner, int exported, int file, const char *name){
    if(owner<=0 || owner==file) return 1;
    return exported && imAllows(file,owner,name);
}

// pesan untuk nama yang di-ex tapi tidak ikut ditulis di im ... from
static void notImported(const char *name, int owner){
    int file=curFile();
    const char *written=imText[file][owner] ? imText[file][owner] : files[owner];
    nrxFail("%s ada di %s tapi tidak ikut di-im (tambahkan di im: im %s from \"%s\")",name,files[owner],name,written);
}

static Var globals[1024];
static int globalCount=0;

// variabel lokal untuk tiap pemanggilan fungsi
typedef struct {
    Var vars[64];
    int count;
} Frame;

static Frame frames[200];
static int depth=0;

static Var *findIn(Var *arr, int count, const char *name){
    for(int i=0;i<count;i++){
        if(strcmp(arr[i].name,name)==0) return &arr[i];
    }
    return NULL;
}

// variabel global yang boleh dipakai dari file ini; milik file sendiri didahulukan.
// kalau ada tapi tidak boleh dipakai (tidak di-ex), dicatat di *hidden
static Var *findGlobal(const char *name, Var **hidden){
    int file=curFile();
    Var *found=NULL;
    for(int i=0;i<globalCount;i++){
        Var *v=&globals[i];
        if(strcmp(v->name,name)!=0) continue;
        if(v->owner==file) return v;
        if(isVisible(v->owner,v->exported,file,name)){
            if(!found) found=v;
        } else if(hidden && !*hidden) {
            *hidden=v;
        }
    }
    return found;
}

static Var *findVarPtr(const char *name){
    if(depth>0){
        Var *v=findIn(frames[depth-1].vars,frames[depth-1].count,name);
        if(v) return v;
    }
    return findGlobal(name,NULL);
}

static void unknownVar(const char *name){
    Var *hidden=NULL;
    findGlobal(name,&hidden);
    if(hidden && hidden->exported) notImported(name,hidden->owner);
    if(hidden) nrxFail("variabel %s ada di %s tapi tidak di-export (tulis di sana: ex %s %s = ...)",name,files[hidden->owner],hidden->locked ? "lock" : "be",name);
    nrxFail("variabel %s belum dibuat (kalau maksudnya teks, pakai tanda kutip: \"%s\")",name,name);
}

Value *findGlobalVal(const char *name){
    Var *v=findIn(globals,globalCount,name);
    return v ? &v->val : NULL;
}

static Var *newVar(Var *arr, int *count, int max, const char *name){
    if(*count>=max) nrxFail("variabel terlalu banyak (maksimal %d)",max);
    Var *v=&arr[(*count)++];
    snprintf(v->name,sizeof v->name,"%s",name);
    v->val=valStr("");
    v->owner=curFile();
    v->exported=0;
    v->locked=0;
    v->kind=K_ANY;
    v->elem=K_ANY;
    v->sname=NULL;
    return v;
}

// jenis variabel baru diambil dari nilai pertamanya
static void takeKind(Var *p, Value v){
    p->kind=valKind(v);
    p->elem=K_ANY;
    p->sname=NULL;
    if(v.type==T_LIST) p->elem=listElem(v.list);
    else if(v.type==T_MAP && v.map->structName) p->sname=v.map->structName;
    else if(v.type==T_MAP) p->elem=mapElem(v.map);
}

// nilai baru untuk variabel yang sudah ada harus sejenis
static void checkVarKind(Var *p, Value *v){
    if(p->kind==K_ANY){
        takeKind(p,*v);
        return;
    }
    if(p->kind==K_LIST && p->elem==K_ANY && v->type==T_LIST) p->elem=listElem(v->list);
    if(p->kind==K_MAP && p->elem==K_ANY && v->type==T_MAP) p->elem=mapElem(v->map);
    int r=fitKind(p->kind,p->sname,p->elem,v);
    if(r==1) nrxFail("%s bertipe int, tidak bisa diisi angka desimal; pakai int(...) untuk membulatkan ke bawah",p->name);
    if(r==2){
        const char *want=p->kind==K_STRUCT ? p->sname : kindName(p->kind);
        if(p->kind==K_LIST || p->kind==K_MAP){
            int e=v->type==T_LIST ? listElem(v->list) : v->type==T_MAP ? mapElem(v->map) : K_ANY;
            if(valKind(*v)==p->kind) nrxFail("%s berisi %s, tidak bisa diisi %s berisi %s",p->name,kindName(p->elem),kindName(p->kind),kindName(e));
        }
        nrxFail("%s bertipe %s, tidak bisa diisi %s",p->name,want,valKindName(*v));
    }
}

void setGlobalVar(const char *name, Value v){
    Var *p=findIn(globals,globalCount,name);
    if(!p){
        p=newVar(globals,&globalCount,1024,name);
        p->owner=-1;
    }
    // library (mis. newInput) boleh mengganti jenis variabelnya
    if(p->kind==K_ANY || fitKind(p->kind,p->sname,p->elem,&v)) takeKind(p,v);
    valFree(&p->val);
    p->val=v;
}

// ubah variabel yang sudah ada (lokal dulu, lalu global); kalau belum ada,
// di dalam fungsi dibuat sebagai variabel lokal, di luar fungsi sebagai global
static void setVar(const char *name, Value v){
    Var *p=findVarPtr(name);
    if(p && p->locked) nrxFail("%s di-lock, nilainya tidak bisa diubah",name);
    if(!p){
        if(depth>0) p=newVar(frames[depth-1].vars,&frames[depth-1].count,64,name);
        else p=newVar(globals,&globalCount,1024,name);
    }
    checkVarKind(p,&v);
    valFree(&p->val);
    p->val=v;
}

// variabel for: di dalam fungsi selalu lokal, walau ada global dengan nama sama
// (kalau tidak, loop di fungsi diam-diam menimpa variabel program utama)
static void setLoopVar(const char *name, Value v){
    if(depth>0 && !findIn(frames[depth-1].vars,frames[depth-1].count,name))
        newVar(frames[depth-1].vars,&frames[depth-1].count,64,name);
    setVar(name,v);
}

// ---------------- helper teks ----------------

// true kalau s diawali kata w (bukan bagian dari kata lain, mis. "ask" vs "asking")
int startsWord(const char *s, const char *w){
    size_t len=strlen(w);
    if(strncmp(s,w,len)!=0) return 0;
    char c=s[len];
    return !(isalnum((unsigned char)c) || c=='_' || c=='.');
}

void trim(char *s){
    int sp=strspn(s," \t");
    memmove(s,s+sp,strlen(s+sp)+1);
    int len=strlen(s);
    while(len>0 && (s[len-1]==' '||s[len-1]=='\t')) s[--len]=0;
}

// baca input dari user; kalau intOnly, ulangi sampai user memasukkan bilangan bulat
void readInput(char *dst, int size, const char *prompt, int intOnly){
    char buf[256];
    for(;;){
        if(prompt && *prompt) printf("%s",prompt);
        fflush(stdout);
        if(!fgets(buf,sizeof buf,stdin)){
            snprintf(dst,size,"%s",intOnly?"0":"");
            return;
        }
        buf[strcspn(buf,"\r\n")]=0;
        if(!intOnly){
            snprintf(dst,size,"%s",buf);
            return;
        }
        char *s=buf;
        while(*s==' '||*s=='\t') s++;
        char *end;
        long val=strtol(s,&end,10);
        while(*end==' '||*end=='\t') end++;
        if(end!=s && *end==0){
            snprintf(dst,size,"%ld",val);
            return;
        }
        printf("%s\n",trMsg("input harus berupa angka bulat (integer), coba lagi",NULL));
    }
}

// pecah isi (...) jadi argumen per koma; koma di dalam "..." , (...) atau [...] tidak dihitung
int splitArgs(const char *s, char out[][256], int max){
    const char *p=strchr(s,'(');
    const char *e=strrchr(s,')');
    if(!p || !e || e<p) return -1;
    int count=0, k=0, dep=0, inq=0;
    for(int i=0;i<max;i++) out[i][0]=0;
    for(const char *c=p+1; c<e; c++){
        if(inq && *c=='\\' && c+1<e){
            // \" dan \\ di dalam teks disalin apa adanya, bukan penutup teks
            if(count<max && k<254){
                out[count][k++]=c[0];
                out[count][k++]=c[1];
                out[count][k]=0;
            }
            c++;
            continue;
        }
        if(*c=='"') inq=!inq;
        else if(!inq && (*c=='('||*c=='['||*c=='{')) dep++;
        else if(!inq && (*c==')'||*c==']'||*c=='}')) dep--;
        if(*c==',' && !inq && dep==0){
            count++;
            k=0;
            continue;
        }
        if(count<max && k<255){
            out[count][k++]=*c;
            out[count][k]=0;
        }
    }
    count++;
    for(int i=0;i<count && i<max;i++) trim(out[i]);
    if(count==1 && out[0][0]==0) return 0;
    return count;
}

// "tambah()" atau "tambah" -> "tambah"
void funcName(const char *s, char *out, int size){
    char tmp[256];
    snprintf(tmp,sizeof tmp,"%s",s);
    char *p=strchr(tmp,'(');
    if(p) *p=0;
    trim(tmp);
    snprintf(out,size,"%s",tmp);
}

static int idStart(char c){
    return isalpha((unsigned char)c) || c=='_';
}

static int idChar(char c){
    return isalnum((unsigned char)c) || c=='_';
}

static int isIdent(const char *s){
    if(!idStart(*s)) return 0;
    for(; *s; s++) if(!idChar(*s)) return 0;
    return 1;
}

static void sleepSeconds(double sec){
    #ifdef _WIN32
    Sleep((DWORD)(sec*1000+0.5));
    #else
    usleep((useconds_t)(sec*1000000));
    #endif
}

// ---------------- fungsi ----------------

typedef struct {
    char name[64];
    int pc;
    char params[8][64];
    char defaults[8][256];   // nilai bawaan parameter (kode nrxl), kosong kalau wajib diisi
    int nparams;
    int nrequired;           // jumlah parameter tanpa nilai bawaan
    int file;       // file tempat fungsi ditulis
    int exported;   // ditulis dengan ex func
    // tipe yang ditulis: func tambah(a int, b int) int. Teksnya disimpan dulu
    // ("int", "list int", "Player"), lalu diubah jadi K_... setelah semua struct dibaca
    char ptype[8][48];
    char rtype[48];
    int pkind[8], pelem[8], rkind, relem;
    const char *psname[8], *rsname;
    int ownerType;  // method milik type ke-berapa (index structs), -1 = fungsi biasa
} Func;

// parameter dan tipe hasil dari baris "func nama(a int, b = 2) int" (atau "(...)" untuk init)
static void parseFuncSig(int i, char *ln, Func *f){
    f->nparams=0;
    f->nrequired=0;
    f->rtype[0]=0;
    // tipe hasil ditulis setelah ): func tambah(a int, b int) int
    char *close=strrchr(ln,')');
    if(close){
        snprintf(f->rtype,sizeof f->rtype,"%s",close+1);
        trim(f->rtype);
    }
    if(!strchr(ln,'(')) return;
    char a[8][256];
    int na=splitArgs(ln,a,8);
    if(na>8) nrxFailAt(i,"fungsi maksimal punya 8 parameter");
    for(int k=0;k<na;k++){
        // parameter biasa (nama) atau dengan nilai bawaan (salam = "Halo")
        char *eq=strchr(a[k],'=');
        f->defaults[k][0]=0;
        if(eq){
            snprintf(f->defaults[k],sizeof f->defaults[k],"%s",eq+1);
            trim(f->defaults[k]);
            *eq=0;
            trim(a[k]);
            if(!f->defaults[k][0]) nrxFailAt(i,"nilai bawaan parameter %s masih kosong",a[k]);
        } else if(k>0 && f->defaults[k-1][0]) {
            nrxFailAt(i,"parameter %s harus ditulis sebelum parameter yang punya nilai bawaan",a[k]);
        } else {
            f->nrequired++;
        }
        // nama tipe: "a int", "skor list int", "p Player"
        f->ptype[k][0]=0;
        char *sp=strpbrk(a[k]," \t");
        if(sp){
            snprintf(f->ptype[k],sizeof f->ptype[k],"%s",sp+1);
            trim(f->ptype[k]);
            *sp=0;
        }
        if(!isIdent(a[k])) nrxFailAt(i,"nama parameter tidak valid: %s",a[k]);
        snprintf(f->params[k],sizeof f->params[k],"%s",a[k]);
    }
    if(na>0) f->nparams=na;
}

static Func funcs[128];
static int funcCount=0;

// fungsi yang boleh dipanggil dari file yang sedang jalan; milik file sendiri didahulukan.
// kalau ada tapi tidak di-ex, baris definisinya dicatat di *hiddenPc
static int findFuncVisible(const char *name, int *hiddenPc){
    int file=curFile(), found=-1;
    for(int i=0;i<funcCount;i++){
        Func *f=&funcs[i];
        if(f->ownerType>=0 || strcmp(f->name,name)!=0) continue;
        if(f->file==file) return f->pc;
        if(isVisible(f->file,f->exported,file,name)){
            if(found<0) found=f->pc;
        } else if(hiddenPc && *hiddenPc<0) {
            *hiddenPc=f->pc;
        }
    }
    return found;
}

// dipakai library (mis. onclick di windowTemplate): cari yang boleh dipakai dulu, kalau tidak ada ambil yang mana saja
int findFunc(const char *name){
    int pc=findFuncVisible(name,NULL);
    if(pc>=0) return pc;
    for(int i=0;i<funcCount;i++){
        if(funcs[i].ownerType<0 && strcmp(funcs[i].name,name)==0) return funcs[i].pc;
    }
    return -1;
}

static Func *funcAt(int pc){
    for(int i=0;i<funcCount;i++){
        if(funcs[i].pc==pc) return &funcs[i];
    }
    return NULL;
}

// jumlah parameter fungsi (dipakai library, mis. onKey mengirim nama tombol kalau fungsinya menerima 1 argumen)
int funcParamCount(int fpc){
    Func *f=funcAt(fpc);
    return f ? f->nparams : 0;
}

// type Player: field bertipe (boleh punya nilai bawaan), init, dan method (func di dalamnya)
//     type Player
//         hp int
//         level int = 1
//         init hp int
//             be own.hp = hp
//         func serang(target Player)
//             be target.hp -= own.level
typedef struct {
    char name[64];
    char fields[16][64];
    char ftype[16][48];      // "int", "list int", "Player"
    char fdef[16][256];      // nilai bawaan (kode nrxl), kosong = nilai kosong sesuai tipe
    int fkind[16], felem[16];
    const char *fsname[16], *fename[16];
    int nfields;
    int file;
    int exported;   // ditulis dengan ex type
    int pc;         // baris "type Player"
    int initPc;     // baris init, -1 kalau tidak ada
} StructDef;

static StructDef structs[64];
static int structCount=0;
static Value newInstance(StructDef *sd, Value *a, int n);
static Value callMethod(Value obj, const char *name, Value *a, int n);

// struct yang boleh dipakai dari file yang sedang jalan; yang ada tapi tidak di-ex dicatat di *hidden
static StructDef *findStruct(const char *name, StructDef **hidden){
    int file=curFile();
    StructDef *found=NULL;
    for(int i=0;i<structCount;i++){
        StructDef *s=&structs[i];
        if(strcmp(s->name,name)!=0) continue;
        if(s->file==file) return s;
        if(isVisible(s->file,s->exported,file,name)){
            if(!found) found=s;
        } else if(hidden && !*hidden) {
            *hidden=s;
        }
    }
    return found;
}

// dua nama yang sama bentrok kalau ada di file yang sama, atau dua-duanya bisa dipakai dari luar
static int nameClash(int fileA, int exA, int fileB, int exB){
    return fileA==fileB || ((fileA==0 || exA) && (fileB==0 || exB));
}

// ---------------- FFI ----------------
// ffi "user32.dll" (
//     func MessageBoxA(hwnd int, teks string, judul string, tipe int) int
// )

enum { FT_VOID, FT_INT, FT_INT64, FT_FLOAT, FT_DOUBLE, FT_STRING, FT_POINTER };
#define FFI_MAX_ARGS 12

typedef struct {
    char name[64];
    char sym[64];       // nama di DLL (beda kalau ditulis: func nama(...) as NamaAsli)
    char dll[260];
    int ptype[FFI_MAX_ARGS];
    int nparams;
    int rtype;
    void *fn;
} FfiFunc;

static FfiFunc ffis[256];
static int ffiCount=0;
static unsigned char inFfi[MAX_LINES];   // baris di dalam blok ffi (bukan fungsi nrxl biasa)

static int ffiType(const char *t){
    if(!strcmp(t,"int")) return FT_INT;
    if(!strcmp(t,"int64")) return FT_INT64;
    if(!strcmp(t,"float")) return FT_FLOAT;
    if(!strcmp(t,"double")) return FT_DOUBLE;
    if(!strcmp(t,"string")) return FT_STRING;
    if(!strcmp(t,"pointer")) return FT_POINTER;
    return -1;
}

static FfiFunc *findFfi(const char *name){
    for(int i=0;i<ffiCount;i++){
        if(!strcmp(ffis[i].name,name)) return &ffis[i];
    }
    return NULL;
}

// baris penutup ")" milik blok ffi di baris pc
static int ffiEnd(int pc){
    int j=pc+1;
    while(j<lineCount && strcmp(lines[j],")")!=0) j++;
    return j<lineCount ? j : -1;
}

// Windows 64-bit: 4 argumen pertama lewat register (bilangan bulat/pointer di RCX, RDX, R8, R9;
// float/double di XMM0-3), sisanya di stack 8 byte per argumen. Setiap argumen disiapkan sebagai
// 8 byte mentah (s[i]); mask menandai posisi 0-3 yang berisi float/double supaya masuk register XMM.
// Argumen lebih dari yang diminta fungsi C tidak berpengaruh, jadi selalu dikirim 12.
typedef uint64_t FU;
typedef double FD;

static FD ffiBits(FU v){
    FD d;
    memcpy(&d,&v,8);
    return d;
}

#define FA_FU(i) s[i]
#define FA_FD(i) ffiBits(s[i])
#define FFI_CALL(R,a,b,c,d) ((R(*)(a,b,c,d,FU,FU,FU,FU,FU,FU,FU,FU))fn)( \
    FA_##a(0),FA_##b(1),FA_##c(2),FA_##d(3),s[4],s[5],s[6],s[7],s[8],s[9],s[10],s[11])
#define FFI_CASES(R) \
    case 0:  return FFI_CALL(R,FU,FU,FU,FU); case 1:  return FFI_CALL(R,FD,FU,FU,FU); \
    case 2:  return FFI_CALL(R,FU,FD,FU,FU); case 3:  return FFI_CALL(R,FD,FD,FU,FU); \
    case 4:  return FFI_CALL(R,FU,FU,FD,FU); case 5:  return FFI_CALL(R,FD,FU,FD,FU); \
    case 6:  return FFI_CALL(R,FU,FD,FD,FU); case 7:  return FFI_CALL(R,FD,FD,FD,FU); \
    case 8:  return FFI_CALL(R,FU,FU,FU,FD); case 9:  return FFI_CALL(R,FD,FU,FU,FD); \
    case 10: return FFI_CALL(R,FU,FD,FU,FD); case 11: return FFI_CALL(R,FD,FD,FU,FD); \
    case 12: return FFI_CALL(R,FU,FU,FD,FD); case 13: return FFI_CALL(R,FD,FU,FD,FD); \
    case 14: return FFI_CALL(R,FU,FD,FD,FD); case 15: return FFI_CALL(R,FD,FD,FD,FD);

static FU ffiCallU(void *fn, int mask, FU *s){
    switch(mask){ FFI_CASES(FU) }
    return 0;
}

static FD ffiCallD(void *fn, int mask, FU *s){
    switch(mask){ FFI_CASES(FD) }
    return 0;
}

static Value ffiCall(FfiFunc *f, Value *a, int n){
    if(n!=f->nparams) nrxFail("%s butuh %d argumen, tapi diberi %d",f->name,f->nparams,n);
    FU s[FFI_MAX_ARGS]={0};
    char *strs[FFI_MAX_ARGS]={0};
    int mask=0;
    for(int i=0;i<n;i++){
        int t=f->ptype[i];
        double d=0;
        if(t==FT_POINTER && a[i].type==T_ARR){
            // array dikirim sebagai double* (isi bisa diubah fungsi C)
            s[i]=(FU)(uintptr_t)a[i].arr->d;
            continue;
        }
        if(t==FT_STRING || (t==FT_POINTER && a[i].type==T_STR && !numLike(a[i],NULL))){
            // teks dikirim sebagai alamat teks (char*)
            strs[i]=valToStr(a[i]);
            s[i]=(FU)(uintptr_t)strs[i];
            continue;
        }
        if(!numLike(a[i],&d)) nrxFail("%s: argumen ke-%d harus angka",f->name,i+1);
        if(t==FT_FLOAT){
            float fv=(float)d;
            uint32_t bits;
            memcpy(&bits,&fv,4);
            s[i]=bits;
            if(i<4) mask|=1<<i;
        } else if(t==FT_DOUBLE){
            memcpy(&s[i],&d,8);
            if(i<4) mask|=1<<i;
        } else if(t==FT_POINTER){
            s[i]=(FU)(uint64_t)d;
        } else {
            s[i]=(FU)(int64_t)d;
        }
    }
    FU r=0;
    FD rd=0;
    if(f->rtype==FT_FLOAT || f->rtype==FT_DOUBLE) rd=ffiCallD(f->fn,mask,s);
    else r=ffiCallU(f->fn,mask,s);
    for(int i=0;i<n;i++) free(strs[i]);
    switch(f->rtype){
        case FT_VOID: return valStr("");
        case FT_INT: return valNum((int32_t)r);
        case FT_INT64: return valNum((double)(int64_t)r);
        case FT_POINTER: return valNum((double)r);
        case FT_STRING: return valStr(r ? (const char*)(uintptr_t)r : "");
        case FT_DOUBLE: return valNum(rd);
        default: {
            // float dikembalikan di 4 byte bawah register XMM0
            FU bits;
            float fv;
            memcpy(&bits,&rd,8);
            uint32_t lo=(uint32_t)bits;
            memcpy(&fv,&lo,4);
            return valNum(fv);
        }
    }
}

// ---- buffer: memori mentah untuk ffi (struct C, array, teks keluaran) ----
// alamatnya disimpan sebagai angka biasa (pointer), sama dengan jenis pointer di ffi

static int bufType(Value t, int *size){
    char *s=valToStr(t);
    static const char *names[]={"int8","int16","int","int64","float","double","pointer"};
    static const int sizes[]={1,2,4,8,4,8,8};
    int r=-1;
    for(int i=0;i<7;i++) if(!strcmp(s,names[i])) r=i;
    if(r<0) nrxFail("jenis %s tidak dikenal (pakai: int8, int16, int, int64, float, double, pointer)",s);
    free(s);
    *size=sizes[r];
    return r;
}

static unsigned char *bufAddr(Value p, Value off, const char *fn){
    double d, o;
    if(!numLike(p,&d) || !numLike(off,&o)) nrxFail("%s: alamat dan offset harus angka",fn);
    if(d==0) nrxFail("%s: alamat 0 (NULL)",fn);
    return (unsigned char*)(uintptr_t)(uint64_t)d+(int64_t)o;
}

// peek(p, offset, jenis) / poke(p, offset, jenis, nilai)
static Value bufPeek(Value *a){
    int size, t=bufType(a[2],&size);
    unsigned char *q=bufAddr(a[0],a[1],"peek");
    int8_t i8; int16_t i16; int32_t i32; int64_t i64; float f; double d; uint64_t u;
    switch(t){
        case 0: memcpy(&i8,q,1); return valNum(i8);
        case 1: memcpy(&i16,q,2); return valNum(i16);
        case 2: memcpy(&i32,q,4); return valNum(i32);
        case 3: memcpy(&i64,q,8); return valNum((double)i64);
        case 4: memcpy(&f,q,4); return valDec(f);
        case 5: memcpy(&d,q,8); return valDec(d);
        default: memcpy(&u,q,8); return valNum((double)u);
    }
}

static void bufPoke(Value *a){
    int size, t=bufType(a[2],&size);
    unsigned char *q=bufAddr(a[0],a[1],"poke");
    double v;
    if(!numLike(a[3],&v)) nrxFail("poke: nilai harus angka");
    int8_t i8=(int8_t)v; int16_t i16=(int16_t)v; int32_t i32=(int32_t)(int64_t)v; int64_t i64=(int64_t)v;
    float f=(float)v; uint64_t u=(uint64_t)v;
    switch(t){
        case 0: memcpy(q,&i8,1); break;
        case 1: memcpy(q,&i16,2); break;
        case 2: memcpy(q,&i32,4); break;
        case 3: memcpy(q,&i64,8); break;
        case 4: memcpy(q,&f,4); break;
        case 5: memcpy(q,&v,8); break;
        default: memcpy(q,&u,8); break;
    }
}

// ---- callback: fungsi nrxl yang bisa dipanggil kode C (mis. qsort, EnumWindows) ----
// Tiap callback memakai satu dari 16 fungsi C tetap. Argumen: sampai 4 bilangan bulat
// atau pointer (register RCX, RDX, R8, R9); hasil fungsi nrxl jadi bilangan bulat.
#define CB_MAX 16
static int cbFunc[CB_MAX];
static int cbCount=0;

static FU cbRun(int slot, FU a0, FU a1, FU a2, FU a3){
    Func *f=funcAt(cbFunc[slot]);
    FU in[4]={a0,a1,a2,a3};
    Value args[4];
    for(int i=0;i<f->nparams;i++) args[i]=valNum((double)(int64_t)in[i]);
    Value r=callFunc(cbFunc[slot],args,f->nparams);
    for(int i=0;i<f->nparams;i++) valFree(&args[i]);
    double d=0;
    numLike(r,&d);
    if(r.type==T_STR && r.sub) d=valTruthy(r);
    valFree(&r);
    return (FU)(int64_t)d;
}

#define CB_T(i) static FU cbT##i(FU a, FU b, FU c, FU d){ return cbRun(i,a,b,c,d); }
CB_T(0) CB_T(1) CB_T(2) CB_T(3) CB_T(4) CB_T(5) CB_T(6) CB_T(7)
CB_T(8) CB_T(9) CB_T(10) CB_T(11) CB_T(12) CB_T(13) CB_T(14) CB_T(15)
static FU (*cbTramp[CB_MAX])(FU,FU,FU,FU)={cbT0,cbT1,cbT2,cbT3,cbT4,cbT5,cbT6,cbT7,
    cbT8,cbT9,cbT10,cbT11,cbT12,cbT13,cbT14,cbT15};

static Value makeCallback(Value name){
    char *s=valToStr(name);
    int fpc=findFuncVisible(s,NULL);
    if(fpc<0) nrxFail("callback: fungsi %s tidak ditemukan",s);
    if(funcAt(fpc)->nparams>4) nrxFail("callback: fungsi %s maksimal punya 4 parameter",s);
    free(s);
    for(int i=0;i<cbCount;i++) if(cbFunc[i]==fpc) return valNum((double)(uintptr_t)cbTramp[i]);
    if(cbCount>=CB_MAX) nrxFail("callback maksimal %d fungsi",CB_MAX);
    cbFunc[cbCount]=fpc;
    return valNum((double)(uintptr_t)cbTramp[cbCount++]);
}

// tandai baris-baris di dalam blok ffi, supaya "func ..." di sana tidak dianggap fungsi nrxl
static void markFfiBlocks(void){
    for(int i=0;i<lineCount;i++){
        if(!startsWord(lines[i],"ffi")) continue;
        int end=ffiEnd(i);
        if(end<0) nrxFailAt(i,"blok ffi belum ditutup dengan )");
        for(int j=i+1;j<end;j++) inFfi[j]=1;
        i=end;
    }
}

// baca semua blok ffi: buka DLL dan cari setiap fungsinya (salah tulis langsung ketahuan)
static void parseFfiBlocks(void){
    for(int i=0;i<lineCount;i++){
        if(!startsWord(lines[i],"ffi")) continue;
        // ffi "user32.dll" (
        char *q1=strchr(lines[i],'"');
        char *q2=q1 ? strchr(q1+1,'"') : NULL;
        char after[64]="";
        if(q2) snprintf(after,sizeof after,"%s",q2+1);
        trim(after);
        if(!q1 || !q2 || q2==q1+1 || strcmp(after,"(")!=0) nrxFailAt(i,"cara menulis: ffi \"file.dll\" (");
        char dll[260];
        snprintf(dll,sizeof dll,"%.*s",(int)(q2-q1-1),q1+1);
        int end=ffiEnd(i);
        #if defined(_WIN32) && defined(_WIN64)
        HMODULE m=LoadLibraryA(dll);
        if(!m) nrxFailAt(i,"ffi: file %s tidak ditemukan",dll);
        #else
        nrxFailAt(i,"ffi hanya didukung di Windows 64-bit");
        #endif
        for(int j=i+1;j<end;j++){
            // func Nama(nama tipe, ...) tipeHasil
            char *ln=lines[j];
            if(!startsWord(ln,"func") || !strchr(ln,'(') || !strrchr(ln,')')){
                nrxFailAt(j,"isi blok ffi: func Nama(nama tipe, ...) tipeHasil");
            }
            if(ffiCount>=256) nrxFailAt(j,"terlalu banyak fungsi ffi (maksimal 256)");
            FfiFunc *f=&ffis[ffiCount];
            memset(f,0,sizeof *f);
            funcName(ln+4,f->name,sizeof f->name);
            if(!isIdent(f->name)) nrxFailAt(j,"nama fungsi ffi tidak valid: %s",f->name);
            if(findFfi(f->name)) nrxFailAt(j,"fungsi ffi %s sudah ada",f->name);
            if(findFunc(f->name)>=0 || findStruct(f->name,NULL)) nrxFailAt(j,"nama %s sudah dipakai fungsi atau struct nrxl",f->name);
            snprintf(f->dll,sizeof f->dll,"%s",dll);
            char a[FFI_MAX_ARGS+1][256];
            int na=splitArgs(ln,a,FFI_MAX_ARGS+1);
            if(na>FFI_MAX_ARGS) nrxFailAt(j,"fungsi ffi maksimal punya %d argumen",FFI_MAX_ARGS);
            for(int k=0;k<na;k++){
                // "hwnd int" atau cukup "int": jenisnya kata terakhir
                char *sp=strrchr(a[k],' ');
                const char *tn=sp ? sp+1 : a[k];
                int t=ffiType(tn);
                if(t<0) nrxFailAt(j,"jenis %s tidak dikenal (pakai: int, int64, float, double, string, pointer)",tn);
                f->ptype[k]=t;
            }
            f->nparams=na<0 ? 0 : na;
            char ret[160];
            snprintf(ret,sizeof ret,"%s",strrchr(ln,')')+1);
            trim(ret);
            // func panjang(s string) int as strlen: nama lain untuk fungsi DLL
            snprintf(f->sym,sizeof f->sym,"%s",f->name);
            char *as=NULL;
            if(startsWord(ret,"as")) as=ret;
            else for(char *q=ret; (q=strstr(q," as"))!=NULL; q++) if(startsWord(q+1,"as")){ as=q+1; break; }
            if(as){
                char *sym=as+2;
                while(*sym==' ' || *sym=='\t') sym++;
                if(!isIdent(sym)) nrxFailAt(j,"cara menulis: func nama(...) tipeHasil as NamaDiDLL");
                snprintf(f->sym,sizeof f->sym,"%s",sym);
                *as=0;
                trim(ret);
            }
            if(!ret[0]) f->rtype=FT_VOID;
            else if((f->rtype=ffiType(ret))<0) nrxFailAt(j,"jenis hasil %s tidak dikenal (pakai: int, int64, float, double, string, pointer, atau kosong)",ret);
            #if defined(_WIN32) && defined(_WIN64)
            f->fn=(void*)GetProcAddress(m,f->sym);
            if(!f->fn) nrxFailAt(j,"ffi: fungsi %s tidak ada di %s",f->sym,dll);
            #endif
            ffiCount++;
        }
        i=end;
    }
}

// index baris pertama setelah blok (baris-baris yang indent-nya lebih dalam dari baris pc)
static int skipBlock(int pc){
    int lind=indents[pc];
    int j=pc+1;
    while(j<lineCount && indents[j]>lind) j++;
    return j;
}

// ---------------- ekspresi ----------------
// urutan prioritas: or < and < not < perbandingan < + - < * / % < -x < x[i]

static const char *ep;
static Value callNamed(const char *name, Value *a, int n);
static Value callNamedId(int id, const char *name, Value *a, int n);

static void ws(void){
    while(*ep==' '||*ep=='\t') ep++;
}

static int matchOp(const char *op){
    ws();
    size_t n=strlen(op);
    if(strncmp(ep,op,n)==0){
        ep+=n;
        return 1;
    }
    return 0;
}

static int matchWord(const char *w){
    ws();
    size_t n=strlen(w);
    if(strncmp(ep,w,n)==0 && !idChar(ep[n])){
        ep+=n;
        return 1;
    }
    return 0;
}

// + menjumlah kalau dua-duanya angka, selain itu menggabung teks: "Umur: " + 5 -> "Umur: 5".
// teks berisi angka ("5", hasil ask) tetap teks; ubah dulu dengan int(...) atau float(...)
static int isDec(Value v){
    return v.type==T_NUM && v.sub;
}

// hasil hitungan: desimal kalau salah satu angkanya desimal, selain itu int
static Value numResult(double r, Value a, Value b){
    return isDec(a) || isDec(b) ? valDec(r) : valNum(r);
}

// / selalu menghasilkan desimal: 7 / 2 = 3.5
static Value binop(Value a, char op, Value b){
    double x, y;
    if(op=='+'){
        if(a.type==T_NUM && b.type==T_NUM) return numResult(a.num+b.num,a,b);
        if(a.type==T_LIST || b.type==T_LIST) nrxFail("operator + tidak bisa untuk list, pakai add(list, nilai)");
        if(a.type==T_MAP || b.type==T_MAP) nrxFail("operator + tidak bisa untuk map, pakai be map[key] = nilai");
        char *s1=valToStr(a), *s2=valToStr(b);
        size_t l1=strlen(s1), l2=strlen(s2);
        char *r=malloc(l1+l2+1);
        memcpy(r,s1,l1);
        memcpy(r+l1,s2,l2+1);
        free(s1);
        free(s2);
        return valStrOwn(r);
    }
    Value bad=a.type!=T_NUM ? a : b;
    if(bad.type==T_STR && !bad.sub) nrxFail("operator %c butuh angka, tapi dapat teks; ubah dulu pakai int(...) atau float(...)",op);
    if(bad.type!=T_NUM) nrxFail("operator %c butuh angka, tapi dapat %s",op,valKindName(bad));
    x=a.num;
    y=b.num;
    if(op=='-') return numResult(x-y,a,b);
    if(op=='*') return numResult(x*y,a,b);
    if(y==0) nrxFail("tidak bisa dibagi dengan nol");
    if(op=='/') return valDec(x/y);
    return numResult(fmod(x,y),a,b);
}

// angka dibandingkan sebagai angka, selain itu sebagai teks; angka dan bukan angka
// tidak pernah sama (operator perbandingan menolaknya lebih dulu, lihat N_CMP)
static int compareVals(Value a, Value b){
    if(a.type==T_NUM && b.type==T_NUM) return (a.num>b.num)-(a.num<b.num);
    if(a.type==T_NUM || b.type==T_NUM) return a.type==T_NUM ? -1 : 1;
    char *s1=valToStr(a), *s2=valToStr(b);
    int c=strcmp(s1,s2);
    free(s1);
    free(s2);
    return (c>0)-(c<0);
}

static int needIndex(List *l, Value idx){
    RC_ALIVE(l,"list dipakai setelah dibebaskan");
    if(idx.type!=T_NUM) nrxFail("index harus angka");
    int i=(int)idx.num;
    if(i<0 || i>=l->count) nrxFail("index %d di luar list (isi list ada %d)",i,l->count);
    return i;
}

// index array: sama dengan needIndex (index dibulatkan ke bawah seperti (int))
static int arrIndex(NumArr *a, double i){
    RC_ALIVE(a,"array dipakai setelah dibebaskan");
    int k=(int)i;
    if(k<0 || k>=a->count) nrxFail("index %d di luar array (isi array ada %d)",k,a->count);
    return k;
}

static Value indexValue(Value v, Value idx){
    if(v.type==T_LIST) return valCopy(v.list->items[needIndex(v.list,idx)]);
    if(v.type==T_ARR){
        if(idx.type!=T_NUM) nrxFail("index harus angka");
        return valDec(v.arr->d[arrIndex(v.arr,idx.num)]);
    }
    if(v.type==T_MAP){
        char *key=valToStr(idx);
        Value *found=mapGet(v.map,key);
        if(!found && v.map->structName) nrxFail("%s tidak punya field %s",v.map->structName,key);
        if(!found) nrxFail("key \"%s\" tidak ada di map (cek dulu dengan contains(map, \"%s\"))",key,key);
        free(key);
        return valCopy(*found);
    }
    if(v.type==T_STR){
        if(idx.type!=T_NUM) nrxFail("index harus angka");
        int i=(int)idx.num, len=strlen(v.str);
        if(i<0 || i>=len) nrxFail("index %d di luar teks (panjang teks %d)",i,len);
        char b[2]={v.str[i],0};
        return valStr(b);
    }
    nrxFail("angka tidak bisa diberi index [ ]");
    return valStr("");
}

// true kalau needle ada di hay: key di map, isi di list, atau bagian dari teks
static int containsValue(Value hay, Value needle){
    if(hay.type==T_MAP){
        char *key=valToStr(needle);
        int found=mapGet(hay.map,key)!=NULL;
        free(key);
        return found;
    }
    if(hay.type==T_LIST){
        for(int i=0;i<hay.list->count;i++){
            if(compareVals(hay.list->items[i],needle)==0) return 1;
        }
        return 0;
    }
    if(hay.type==T_ARR){
        if(needle.type!=T_NUM) return 0;
        for(int i=0;i<hay.arr->count;i++) if(hay.arr->d[i]==needle.num) return 1;
        return 0;
    }
    char *s=valToStr(hay), *sub=valToStr(needle);
    int found=strstr(s,sub)!=NULL;
    free(s);
    free(sub);
    return found;
}

// ---- AST ----
// teks ekspresi dibaca sekali jadi pohon Node, lalu pohonnya yang dijalankan.
// pohon disimpan di cache berdasarkan isi teksnya, jadi baris yang diulang (loop,
// fungsi) tidak dibaca ulang. Pohon ini juga yang nanti dipakai compiler.
enum {
    N_LIT,      // angka, teks, true/false (lit)
    N_VAR,      // nama
    N_LIST,     // [kids...]
    N_MAP,      // {k0: v0, k1: v1}: kids berpasangan
    N_CALL,     // nama(kids...)
    N_INDEX,    // kids[0][kids[1]]
    N_FIELD,    // kids[0].nama
    N_METHOD,   // kids[0].nama(kids[1..])
    N_NEG,      // -kids[0]
    N_BIN,      // kids[0] op kids[1], op salah satu + - * / %
    N_CMP,      // kids[0] cmp kids[1], op = index di cmpOps
    N_IN,       // kids[0] in kids[1]; op=1 untuk not in
    N_NOT, N_AND, N_OR,
    N_CONCAT    // kids berjejer digabung jadi teks: "skor: " skor
};

typedef struct Node {
    int kind;
    int op;
    Value lit;
    char *name;
    struct Node **kids;
    int n;
} Node;

static const char *cmpOps[]={"<=",">=","==","!=","<",">"};

static Node *newNode(int kind){
    Node *x=calloc(1,sizeof(Node));
    x->kind=kind;
    return x;
}

static void addKid(Node *x, Node *k){
    x->kids=realloc(x->kids,(x->n+1)*sizeof(Node*));
    x->kids[x->n++]=k;
}

static Node *node2(int kind, int op, Node *a, Node *b){
    Node *x=newNode(kind);
    x->op=op;
    addKid(x,a);
    if(b) addKid(x,b);
    return x;
}

static char *readName(void){
    char name[64];
    int k=0;
    while(idChar(*ep)){
        if(k<63) name[k++]=*ep;
        ep++;
    }
    name[k]=0;
    return strdup(name);
}

static Node *pOr(void);

// satu argumen fungsi; boleh berjejer seperti print: writeFile(f, "skor: " skor)
static Node *pArg(void){
    Node *v=pOr();
    ws();
    if(!*ep || *ep==',' || *ep==')') return v;
    Node *c=newNode(N_CONCAT);
    addKid(c,v);
    while(ws(), *ep && *ep!=',' && *ep!=')') addKid(c,pOr());
    return c;
}

// (a, b, ...) setelah nama fungsi atau method; ep tepat setelah (
static void pArgs(Node *x, const char *name){
    int n=0;
    if(matchOp(")")) return;
    do {
        if(n++>=16) nrxFail("argumen %s terlalu banyak",name);
        addKid(x,pArg());
    } while(matchOp(","));
    if(!matchOp(")")) nrxFail("kurang tanda ) setelah argumen %s",name);
}

static Node *pPrimary(void){
    ws();
    char c=*ep;
    if(isdigit((unsigned char)c) || (c=='.' && isdigit((unsigned char)ep[1]))){
        // 5 -> int, 2.5 / 5.0 / 1e3 -> desimal
        char *end;
        double d=strtod(ep,&end);
        int dec=0;
        for(const char *c=ep; c<end; c++) if(*c=='.' || *c=='e' || *c=='E') dec=1;
        ep=end;
        Node *x=newNode(N_LIT);
        x->lit=dec ? valDec(d) : valNum(d);
        return x;
    }
    if(c=='"'){
        // \n baris baru, \t tab, \" tanda kutip, \\ garis miring; selain itu dibiarkan apa adanya
        ep++;
        char *buf=malloc(strlen(ep)+1);
        int k=0;
        while(*ep && *ep!='"'){
            if(*ep=='\\' && ep[1]){
                char e=ep[1];
                if(e=='n'){ buf[k++]='\n'; ep+=2; continue; }
                if(e=='t'){ buf[k++]='\t'; ep+=2; continue; }
                if(e=='r'){ buf[k++]='\r'; ep+=2; continue; }
                if(e=='"' || e=='\\'){ buf[k++]=e; ep+=2; continue; }
            }
            buf[k++]=*ep++;
        }
        if(*ep!='"') nrxFail("tanda kutip \" belum ditutup");
        ep++;
        buf[k]=0;
        Node *x=newNode(N_LIT);
        x->lit=valStrOwn(buf);
        return x;
    }
    if(c=='{'){
        // map: {"nama": "Budi", "umur": 20}
        ep++;
        Node *x=newNode(N_MAP);
        if(!matchOp("}")){
            do {
                addKid(x,pOr());
                if(!matchOp(":")) nrxFail("di dalam map, setiap key harus diikuti : lalu nilainya");
                addKid(x,pOr());
            } while(matchOp(","));
            if(!matchOp("}")) nrxFail("kurang tanda }");
        }
        return x;
    }
    if(c=='('){
        ep++;
        Node *x=pOr();
        if(!matchOp(")")) nrxFail("kurang tanda )");
        return x;
    }
    if(c=='['){
        ep++;
        Node *x=newNode(N_LIST);
        if(!matchOp("]")){
            do addKid(x,pOr()); while(matchOp(","));
            if(!matchOp("]")) nrxFail("kurang tanda ]");
        }
        return x;
    }
    if(idStart(c)){
        char *name=readName();
        const char *after=ep;
        ws();
        if(*ep=='('){
            ep++;
            Node *x=newNode(N_CALL);
            x->name=name;
            pArgs(x,name);
            return x;
        }
        ep=after;
        if(strcmp(name,"true")==0 || strcmp(name,"false")==0){
            Node *x=newNode(N_LIT);
            x->lit=valBool(name[0]=='t');
            free(name);
            return x;
        }
        Node *x=newNode(N_VAR);
        x->name=name;
        return x;
    }
    if(!c) nrxFail("ekspresi tidak lengkap");
    nrxFail("tidak dimengerti: %s",ep);
    return NULL;
}

// x[i], x["key"], x.field, dan x.method(...)
static Node *pPostfix(void){
    Node *v=pPrimary();
    for(;;){
        ws();
        if(*ep=='['){
            ep++;
            Node *idx=pOr();
            if(!matchOp("]")) nrxFail("kurang tanda ]");
            v=node2(N_INDEX,0,v,idx);
        } else if(*ep=='.' && idStart(ep[1])){
            ep++;
            char *field=readName();
            ws();
            if(*ep=='('){
                ep++;
                Node *x=node2(N_METHOD,0,v,NULL);
                x->name=field;
                pArgs(x,field);
                v=x;
            } else {
                Node *x=node2(N_FIELD,0,v,NULL);
                x->name=field;
                v=x;
            }
        } else {
            return v;
        }
    }
}

static Node *pUnary(void){
    ws();
    if(*ep=='-'){
        ep++;
        return node2(N_NEG,0,pUnary(),NULL);
    }
    return pPostfix();
}

static Node *pMul(void){
    Node *v=pUnary();
    for(;;){
        ws();
        char op=*ep;
        if(op!='*' && op!='/' && op!='%') return v;
        ep++;
        v=node2(N_BIN,op,v,pUnary());
    }
}

static Node *pAdd(void){
    Node *v=pMul();
    for(;;){
        ws();
        char op=*ep;
        if(op!='+' && op!='-') return v;
        ep++;
        v=node2(N_BIN,op,v,pMul());
    }
}

static Node *pCmp(void){
    Node *a=pAdd();
    // x in list / x not in list
    ws();
    const char *save=ep;
    int negate=matchWord("not");
    if(matchWord("in")) return node2(N_IN,negate,a,pAdd());
    ep=save;
    for(int i=0;i<6;i++){
        if(matchOp(cmpOps[i])) return node2(N_CMP,i,a,pAdd());
    }
    return a;
}

static Node *pNot(void){
    if(matchWord("not")) return node2(N_NOT,0,pNot(),NULL);
    return pCmp();
}

static Node *pAnd(void){
    Node *v=pNot();
    while(matchWord("and")) v=node2(N_AND,0,v,pNot());
    return v;
}

static Node *pOr(void){
    Node *v=pAnd();
    while(matchWord("or")) v=node2(N_OR,0,v,pAnd());
    return v;
}

// ---- menjalankan pohon ----
// tiap jenis node punya satu fungsi rt_*; interpreter (evalNode) dan kode C hasil compile
// (nrxlang build) memanggil fungsi yang sama, jadi hasil dan pesan errornya selalu sama.
// Semua rt_* menerima nilai milik pemanggil dan membebaskannya.

Value rt_var(const char *name){
    Var *v=findVarPtr(name);
    if(!v) unknownVar(name);
    return valCopy(v->val);
}

int rt_truthy(Value v){
    int t=valTruthy(v);
    valFree(&v);
    return t;
}

static void freeArgs(Value *args, int n){
    for(int i=0;i<n;i++) valFree(&args[i]);
}

Value rt_list(Value *a, int n){
    List *l=listNew();
    for(int i=0;i<n;i++){
        sameKind(&l->elem,&l->ename,&a[i],"list");
        listPush(l,a[i]);
    }
    // [1, 2.5]: semua jadi desimal
    if(l->elem==K_DOUBLE) for(int i=0;i<l->count;i++) l->items[i].sub=1;
    return valList(l);
}

// kv: key0, nilai0, key1, nilai1, ...; n = jumlah isi kv
Value rt_map(Value *kv, int n){
    Map *m=mapNew();
    for(int i=0;i+1<n;i+=2){
        char *ks=valToStr(kv[i]);
        valFree(&kv[i]);
        sameKind(&m->elem,&m->ename,&kv[i+1],"map");
        mapSet(m,ks,kv[i+1]);
        free(ks);
    }
    if(m->elem==K_DOUBLE) for(int i=0;i<m->count;i++) m->vals[i].sub=1;
    return valMap(m);
}

Value rt_call(const char *name, Value *a, int n){
    Value r=callNamed(name,a,n);
    freeArgs(a,n);
    return r;
}

// id dari builtinId saat build (-1 = bukan fungsi bawaan inti)
Value rt_callb(int id, const char *name, Value *a, int n){
    Value r=callNamedId(id,name,a,n);
    freeArgs(a,n);
    return r;
}

// array di variabel C: isi dibaca/diubah langsung sebagai double
double rt_arr_get(Value *a, double i){
    return a->arr->d[arrIndex(a->arr,i)];
}

// op: '=' '+' '-' '*' (sama dengan binop untuk dua angka desimal)
void rt_arr_set(Value *a, double i, int op, double v){
    double *p=&a->arr->d[arrIndex(a->arr,i)];
    if(op=='=') *p=v;
    else if(op=='+') *p+=v;
    else if(op=='-') *p-=v;
    else *p*=v;
}

// a[i] dengan a variabel C: list tidak perlu disalin dulu
Value rt_index_at(Value *v, Value idx){
    Value r=indexValue(*v,idx);
    valFree(&idx);
    return r;
}

Value rt_index(Value v, Value idx){
    Value r=indexValue(v,idx);
    valFree(&v);
    valFree(&idx);
    return r;
}

Value rt_field(Value v, const char *name){
    if(v.type!=T_MAP) nrxFail(".%s hanya bisa dipakai untuk map atau struct",name);
    Value idx=valStr(name);
    return rt_index(v,idx);
}

// dicek sebelum argumen method dihitung
void rt_method_check(Value *o, const char *name){
    if(o->type!=T_MAP) nrxFail(".%s hanya bisa dipakai untuk map atau struct",name);
    if(!o->map->structName) nrxFail("tidak dimengerti: %s","(");
}

// budi.serang(sari): panggil method
Value rt_method(Value o, const char *name, Value *a, int n){
    Value r=callMethod(o,name,a,n);
    freeArgs(a,n);
    valFree(&o);
    return r;
}

Value rt_neg(Value r){
    if(r.type!=T_NUM) nrxFail("tanda - butuh angka");
    double d=r.num;
    int dec=isDec(r);
    valFree(&r);
    return dec ? valDec(-d) : valNum(-d);
}

Value rt_bin(Value a, int op, Value b){
    Value r=binop(a,(char)op,b);
    valFree(&a);
    valFree(&b);
    return r;
}

// op = index di cmpOps: <= >= == != < >
Value rt_cmp(Value a, int op, Value b){
    if((a.type==T_NUM)!=(b.type==T_NUM))
        nrxFail("tidak bisa membandingkan %s dengan %s; ubah dulu pakai int(...), float(...), atau str(...)",valKindName(a),valKindName(b));
    int c=compareVals(a,b);
    valFree(&a);
    valFree(&b);
    int r;
    switch(op){
        case 0: r=c<=0; break;
        case 1: r=c>=0; break;
        case 2: r=c==0; break;
        case 3: r=c!=0; break;
        case 4: r=c<0; break;
        default: r=c>0;
    }
    return valBool(r);
}

Value rt_in(Value a, int negate, Value b){
    int r=containsValue(b,a);
    valFree(&a);
    valFree(&b);
    return valBool(negate ? !r : r);
}

// bagian teks berjejer langsung dijadikan teks begitu dihitung: print l " " takeLast(l)
// mencetak l sebelum isinya diambil
Value rt_tostr(Value v){
    if(v.type==T_STR && !v.sub) return v;
    char *s=valToStr(v);
    valFree(&v);
    return valStrOwn(s);
}

Value rt_concat(Value *a, int n){
    char *s=strdup("");
    for(int i=0;i<n;i++){
        char *t=valToStr(a[i]);
        valFree(&a[i]);
        s=realloc(s,strlen(s)+strlen(t)+1);
        strcat(s,t);
        free(t);
    }
    return valStrOwn(s);
}

static Value evalNode(Node *x);

static int nodeTruthy(Node *x){
    return rt_truthy(evalNode(x));
}

// isi node berurutan dari kiri ke kanan (urutannya penting kalau ada fungsi yang print)
static int evalKids(Node *x, int from, Value *out){
    int n=0;
    for(int i=from;i<x->n;i++) out[n++]=evalNode(x->kids[i]);
    return n;
}

static Value evalNode(Node *x){
    switch(x->kind){
    case N_LIT:
        return valCopy(x->lit);
    case N_VAR:
        return rt_var(x->name);
    case N_LIST: case N_MAP: case N_CONCAT: {
        Value stackBuf[16];
        Value *a=x->n<=16 ? stackBuf : malloc(x->n*sizeof(Value));
        int n=0;
        for(int i=0;i<x->n;i++){
            a[n]=evalNode(x->kids[i]);
            if(x->kind==N_CONCAT) a[n]=rt_tostr(a[n]);
            n++;
        }
        Value r=x->kind==N_LIST ? rt_list(a,n) : x->kind==N_MAP ? rt_map(a,n) : rt_concat(a,n);
        if(a!=stackBuf) free(a);
        return r;
    }
    case N_CALL: {
        Value args[16];
        int n=evalKids(x,0,args);
        return rt_call(x->name,args,n);
    }
    case N_INDEX: {
        Value v=evalNode(x->kids[0]);
        Value idx=evalNode(x->kids[1]);
        return rt_index(v,idx);
    }
    case N_FIELD:
        return rt_field(evalNode(x->kids[0]),x->name);
    case N_METHOD: {
        Value v=evalNode(x->kids[0]);
        rt_method_check(&v,x->name);
        Value args[16];
        int n=evalKids(x,1,args);
        return rt_method(v,x->name,args,n);
    }
    case N_NEG:
        return rt_neg(evalNode(x->kids[0]));
    case N_BIN: case N_CMP: case N_IN: {
        Value a=evalNode(x->kids[0]);
        Value b=evalNode(x->kids[1]);
        if(x->kind==N_BIN) return rt_bin(a,x->op,b);
        if(x->kind==N_CMP) return rt_cmp(a,x->op,b);
        return rt_in(a,x->op,b);
    }
    case N_NOT:
        return valBool(!nodeTruthy(x->kids[0]));
    // and/or berhenti lebih awal: kanan tidak dihitung kalau kiri sudah menentukan,
    // jadi aman menulis: k in m and m[k] > 0
    case N_AND:
        return valBool(nodeTruthy(x->kids[0]) && nodeTruthy(x->kids[1]));
    case N_OR:
        return valBool(nodeTruthy(x->kids[0]) || nodeTruthy(x->kids[1]));
    }
    return valStr("");
}

// ---- cache pohon per teks ----
// mode 'e' = satu ekspresi, 's' = beberapa ekspresi berjejer (print, ask, ...)

typedef struct { char *src; char mode; Node *node; } AstSlot;
static AstSlot *astSlots;
static int astCap, astCount;

static unsigned astHash(const char *s, char mode){
    unsigned h=2166136261u^(unsigned char)mode;
    for(;*s;s++) h=(h^(unsigned char)*s)*16777619u;
    return h;
}

static Node *parseText(const char *src, char mode){
    const char *saved=ep;
    ep=src;
    Node *x;
    if(mode=='e'){
        x=pOr();
        ws();
        if(*ep) nrxFail("tidak dimengerti: %s",ep);
    } else {
        ws();
        if(!*ep){
            x=newNode(N_LIT);
            x->lit=valStr("");
        } else {
            // satu ekspresi: jenisnya tetap; lebih dari satu: digabung jadi teks
            x=pOr();
            ws();
            if(*ep){
                Node *c=newNode(N_CONCAT);
                addKid(c,x);
                while(ws(), *ep) addKid(c,pOr());
                x=c;
            }
        }
    }
    ep=saved;
    return x;
}

static Node *astFor(const char *src, char mode){
    if(astCount*2>=astCap){
        int oldCap=astCap;
        AstSlot *old=astSlots;
        astCap=astCap ? astCap*2 : 256;
        astSlots=calloc(astCap,sizeof(AstSlot));
        astCount=0;
        for(int i=0;i<oldCap;i++){
            if(!old[i].src) continue;
            unsigned j=astHash(old[i].src,old[i].mode)&(astCap-1);
            while(astSlots[j].src) j=(j+1)&(astCap-1);
            astSlots[j]=old[i];
            astCount++;
        }
        free(old);
    }
    unsigned j=astHash(src,mode)&(astCap-1);
    while(astSlots[j].src){
        if(astSlots[j].mode==mode && strcmp(astSlots[j].src,src)==0) return astSlots[j].node;
        j=(j+1)&(astCap-1);
    }
    // dibaca dulu baru disimpan: kalau ada error baca, cache tidak berisi pohon setengah jadi
    Node *x=parseText(src,mode);
    astSlots[j].src=strdup(src);
    astSlots[j].mode=mode;
    astSlots[j].node=x;
    astCount++;
    return x;
}

Value evalExpr(const char *src){
    return evalNode(astFor(src,'e'));
}

// "a" b "c": beberapa ekspresi berjejer digabung jadi satu teks; kalau cuma satu, jenisnya tetap
Value evalSeq(const char *src){
    return evalNode(astFor(src,'s'));
}

char *evalSeqStr(const char *src){
    Value v=evalSeq(src);
    char *s=valToStr(v);
    valFree(&v);
    return s;
}

static int evalCond(const char *src){
    Value v=evalExpr(src);
    int t=valTruthy(v);
    valFree(&v);
    return t;
}

// ---------------- fungsi bawaan ----------------

static void needArgs(const char *name, int n, int want){
    if(n!=want) nrxFail("%s butuh %d argumen, tapi diberi %d",name,want,n);
}

// argumen angka untuk fungsi bawaan; teks berisi angka harus diubah dulu dengan int/float
static double needNum(const char *name, Value v){
    if(v.type!=T_NUM){
        char *s=valToStr(v);
        nrxFail("%s(): \"%s\" bukan angka",name,s);
    }
    return v.num;
}

static List *needList(const char *name, Value v){
    if(v.type!=T_LIST) nrxFail("argumen pertama %s harus list",name);
    return v.list;
}

// untuk add/takeLast/remove: list atau map yang di-lock tidak boleh diubah isinya
static void needUnlocked(const char *name, Value v){
    if((v.type==T_LIST && v.list->locked) || (v.type==T_MAP && v.map->locked) || (v.type==T_ARR && v.arr->locked)){
        nrxFail("%s: isinya di-lock, tidak bisa diubah",name);
    }
}

static int sortCmp(const void *x, const void *y){
    return compareVals(*(const Value*)x,*(const Value*)y);
}

// index awal..akhir untuk slice; angka negatif dihitung dari belakang, lalu dibatasi ke 0..len
static int clampIndex(double d, int len){
    int i=(int)d;
    if(i<0) i+=len;
    if(i<0) i=0;
    if(i>len) i=len;
    return i;
}

// argumen range: batas awal, akhir, dan langkahnya (dipakai juga for i in range(...) hasil compile)
static void rangeArgs(Value *a, int n, double *from, double *to, double *step){
    const char *name="range";
    if(n<1 || n>3) nrxFail("range butuh 1 sampai 3 argumen: range(akhir), range(awal, akhir), range(awal, akhir, langkah)");
    *from=0;
    *step=1;
    if(n==1) *to=needNum(name,a[0]);
    else {
        *from=needNum(name,a[0]);
        *to=needNum(name,a[1]);
        if(n==3) *step=needNum(name,a[2]);
    }
    if(*step==0) nrxFail("range: langkah tidak boleh 0");
}

// fungsi bawaan inti punya nomor: nama dicari sekali (pencarian biner), lalu
// callNamedId membandingkan angka, bukan teks. Program hasil build menyimpan nomornya
// langsung (rt_callb), jadi tidak ada pencarian nama sama sekali saat jalan.
enum { B_abs, B_add, B_array, B_atan2, B_callback, B_ceil, B_contains, B_cos, B_date, B_exit, B_filled, B_float, B_floor, B_freeBuffer, B_im, B_indexOf, B_int, B_join, B_keys, B_len, B_lower, B_max, B_min, B_newBuffer, B_peek, B_peekText, B_pi, B_poke, B_pow, B_range, B_remove, B_replace, B_reverse, B_round, B_sin, B_slice, B_sort, B_split, B_sqrt, B_str, B_substr, B_takeLast, B_tan, B_time, B_timer, B_trim, B_type, B_typeOf, B_upper, B_values, B_write, B_COUNT };
static const char *builtinNames[B_COUNT]={"abs","add","array","atan2","callback","ceil","contains","cos","date","exit","filled","float","floor","freeBuffer","im","indexOf","int","join","keys","len","lower","max","min","newBuffer","peek","peekText","pi","poke","pow","range","remove","replace","reverse","round","sin","slice","sort","split","sqrt","str","substr","takeLast","tan","time","timer","trim","type","typeOf","upper","values","write"};

// nomor fungsi bawaan inti, atau -1 (fungsi library, ffi, atau buatan sendiri)
static int builtinId(const char *name){
    int lo=0, hi=B_COUNT-1;
    while(lo<=hi){
        int mid=(lo+hi)/2, c=strcmp(name,builtinNames[mid]);
        if(!c) return mid;
        if(c<0) hi=mid-1;
        else lo=mid+1;
    }
    return -1;
}

static Value callNamedId(int id, const char *name, Value *a, int n){
    if(id==B_im) nrxFail("im hanya untuk memasukkan file .nrxl, contoh: im hitung (bukan fungsi im())");
    if(id==B_type) nrxFail("type adalah kata khusus untuk membuat tipe baru (belum tersedia); untuk melihat jenis nilai pakai typeOf(x)");
    // fungsi ffi yang ditulis sendiri didahulukan, walaupun namanya sama dengan fungsi bawaan (sqrt, abs, ...)
    FfiFunc *ff=findFfi(name);
    if(ff) return ffiCall(ff,a,n);
    if(id==B_newBuffer){
        double sz;
        if(n!=1 || !numLike(a[0],&sz) || sz<1) nrxFail("newBuffer butuh 1 angka (ukuran byte): newBuffer(16)");
        if(sz>1e9) nrxFail("newBuffer: ukuran terlalu besar");
        void *p=calloc(1,(size_t)sz);
        if(!p) nrxFail("newBuffer: memori tidak cukup");
        return valNum((double)(uintptr_t)p);
    }
    if(id==B_freeBuffer){
        double d;
        if(n!=1 || !numLike(a[0],&d)) nrxFail("freeBuffer butuh 1 alamat: freeBuffer(p)");
        free((void*)(uintptr_t)(uint64_t)d);
        return valStr("");
    }
    if(id==B_peek){
        if(n!=3) nrxFail("peek butuh 3 argumen: peek(p, offset, \"int\")");
        return bufPeek(a);
    }
    if(id==B_poke){
        if(n!=4) nrxFail("poke butuh 4 argumen: poke(p, offset, \"int\", nilai)");
        bufPoke(a);
        return valStr("");
    }
    if(id==B_peekText){
        if(n!=1 && n!=2) nrxFail("peekText butuh 1 atau 2 argumen: peekText(p) atau peekText(p, offset)");
        Value zero=valNum(0);
        unsigned char *q=bufAddr(a[0],n==2 ? a[1] : zero,"peekText");
        return valStr((const char*)q);
    }
    if(id==B_callback){
        if(n!=1) nrxFail("callback butuh 1 argumen: callback(\"namaFungsi\")");
        return makeCallback(a[0]);
    }
    if(id==B_write){
        // seperti print, tapi tanpa ganti baris di akhir: write("\rLoading ", n, "%")
        for(int i=0;i<n;i++){
            char *s=valToStr(a[i]);
            fputs(s,stdout);
            free(s);
        }
        fflush(stdout);
        return valStr("");
    }
    if(id==B_len){
        needArgs(name,n,1);
        if(a[0].type==T_LIST) return valNum(a[0].list->count);
        if(a[0].type==T_MAP) return valNum(a[0].map->count);
        if(a[0].type==T_ARR) return valNum(a[0].arr->count);
        char *s=valToStr(a[0]);
        Value r=valNum(strlen(s));
        free(s);
        return r;
    }
    if(id==B_add){
        needArgs(name,n,2);
        needUnlocked(name,a[0]);
        if(a[0].type==T_ARR){
            if(a[1].type!=T_NUM) nrxFail("isi array hanya angka, tidak bisa diisi %s",valKindName(a[1]));
            arrPush(a[0].arr,a[1].num);
            return valStr("");
        }
        List *l=needList(name,a[0]);
        Value v=valCopy(a[1]);
        listElem(l);
        fitElem(&l->elem,&l->ename,&v,"list");
        listPush(l,v);
        return valStr("");
    }
    if(id==B_takeLast){
        needArgs(name,n,1);
        needUnlocked(name,a[0]);
        List *l=needList(name,a[0]);
        if(l->count==0) nrxFail("takeLast: list sudah kosong");
        return l->items[--l->count];
    }
    if(id==B_remove){
        needArgs(name,n,2);
        needUnlocked(name,a[0]);
        if(a[0].type==T_MAP){
            char *key=valToStr(a[1]);
            Value r=mapRemove(a[0].map,key);
            free(key);
            return r;
        }
        List *l=needList(name,a[0]);
        int i=needIndex(l,a[1]);
        Value r=l->items[i];
        memmove(&l->items[i],&l->items[i+1],(l->count-i-1)*sizeof(Value));
        l->count--;
        return r;
    }
    if(id==B_keys || id==B_values){
        needArgs(name,n,1);
        if(a[0].type!=T_MAP) nrxFail("%s butuh map: %s({\"a\": 1})",name,name);
        List *l=listNew();
        for(int i=0;i<a[0].map->count;i++){
            listPush(l,name[0]=='k' ? valStr(a[0].map->keys[i]) : valCopy(a[0].map->vals[i]));
        }
        return valList(l);
    }
    if(id==B_range){
        // range(5) -> [0..4], range(2, 5) -> [2, 3, 4], range(10, 0, -2) -> [10, 8, 6, 4, 2]
        double from, to, step;
        rangeArgs(a,n,&from,&to,&step);
        List *l=listNew();
        for(double x=from; step>0 ? x<to : x>to; x+=step){
            if(l->count>=10000000) nrxFail("range terlalu besar");
            listPush(l,valNum(x));
        }
        return valList(l);
    }
    if(id==B_abs || id==B_floor || id==B_ceil || id==B_sqrt){
        needArgs(name,n,1);
        double d=needNum(name,a[0]);
        if(name[0]=='a') return isDec(a[0]) ? valDec(fabs(d)) : valNum(fabs(d));
        if(name[0]=='f') return valNum(floor(d));
        if(name[0]=='c') return valNum(ceil(d));
        if(d<0) nrxFail("sqrt: tidak bisa akar dari angka negatif");
        return valDec(sqrt(d));
    }
    if(id==B_sin || id==B_cos || id==B_tan){
        // sudut dalam radian (pi() = 180 derajat), sama dengan bahasa lain
        needArgs(name,n,1);
        double d=needNum(name,a[0]);
        return valDec(name[0]=='s' ? sin(d) : name[0]=='c' ? cos(d) : tan(d));
    }
    if(id==B_atan2){
        // atan2(y, x): sudut (radian) dari titik (x, y), -pi sampai pi
        needArgs(name,n,2);
        return valDec(atan2(needNum(name,a[0]),needNum(name,a[1])));
    }
    if(id==B_pi){
        needArgs(name,n,0);
        return valDec(3.14159265358979323846);
    }
    if(id==B_round){
        // round(2.567) -> 3, round(2.567, 2) -> 2.57
        if(n!=1 && n!=2) nrxFail("round butuh 1 atau 2 argumen: round(angka) atau round(angka, digit)");
        double d=needNum(name,a[0]);
        double f=n==2 ? pow(10,needNum(name,a[1])) : 1;
        return n==2 ? valDec(round(d*f)/f) : valNum(round(d));
    }
    if(id==B_pow){
        needArgs(name,n,2);
        // pow(2, 8) = 256 tetap int; pangkat negatif atau desimal menghasilkan desimal
        double b=needNum(name,a[0]), e=needNum(name,a[1]);
        if(!isDec(a[0]) && !isDec(a[1]) && e>=0) return valNum(pow(b,e));
        return valDec(pow(b,e));
    }
    if(id==B_min || id==B_max){
        // min(3, 1, 2) atau min([3, 1, 2])
        Value *items=a;
        int count=n;
        if(n==1 && a[0].type==T_LIST){
            items=a[0].list->items;
            count=a[0].list->count;
        }
        if(count==0) nrxFail("%s butuh minimal 1 angka",name);
        double best=needNum(name,items[0]);
        for(int i=1;i<count;i++){
            double d=needNum(name,items[i]);
            if(name[1]=='i' ? d<best : d>best) best=d;
        }
        return valNum(best);
    }
    if(id==B_str){
        needArgs(name,n,1);
        return valStrOwn(valToStr(a[0]));
    }
    if(id==B_trim){
        // buang spasi/tab/baris baru di awal dan akhir teks
        needArgs(name,n,1);
        char *s=valToStr(a[0]);
        char *start=s;
        while(*start && strchr(" \t\r\n",*start)) start++;
        size_t len=strlen(start);
        while(len>0 && strchr(" \t\r\n",start[len-1])) len--;
        Value r=valStr("");
        free(r.str);
        r.str=malloc(len+1);
        memcpy(r.str,start,len);
        r.str[len]=0;
        free(s);
        return r;
    }
    if(id==B_upper || id==B_lower){
        needArgs(name,n,1);
        char *s=valToStr(a[0]);
        for(char *c=s; *c; c++) *c=name[0]=='u' ? toupper((unsigned char)*c) : tolower((unsigned char)*c);
        return valStrOwn(s);
    }
    if(id==B_split){
        // split(teks, pemisah); split(teks) memecah per spasi; pemisah "" memecah per huruf
        if(n!=1 && n!=2) nrxFail("split butuh 1 atau 2 argumen: split(teks, pemisah)");
        char *s=valToStr(a[0]);
        List *l=listNew();
        if(n==1){
            char *tok=strtok(s," \t\r\n");
            while(tok){
                listPush(l,valStr(tok));
                tok=strtok(NULL," \t\r\n");
            }
        } else {
            char *sep=valToStr(a[1]);
            size_t sl=strlen(sep);
            if(sl==0){
                for(char *c=s; *c; c++){
                    char b[2]={*c,0};
                    listPush(l,valStr(b));
                }
            } else {
                char *p=s, *hit;
                while((hit=strstr(p,sep))){
                    *hit=0;
                    listPush(l,valStr(p));
                    p=hit+sl;
                }
                listPush(l,valStr(p));
            }
            free(sep);
        }
        free(s);
        return valList(l);
    }
    if(id==B_join){
        // join(list, pemisah); tanpa pemisah isi list langsung disambung
        if(n!=1 && n!=2) nrxFail("join butuh 1 atau 2 argumen: join(list, pemisah)");
        List *l=needList(name,a[0]);
        char *sep=n==2 ? valToStr(a[1]) : strdup("");
        size_t cap=64, len=0, sl=strlen(sep);
        char *out=malloc(cap);
        out[0]=0;
        for(int i=0;i<l->count;i++){
            char *s=valToStr(l->items[i]);
            size_t need=len+strlen(s)+sl+1;
            if(need>cap){
                while(need>cap) cap*=2;
                out=realloc(out,cap);
            }
            if(i>0){
                memcpy(out+len,sep,sl);
                len+=sl;
            }
            strcpy(out+len,s);
            len+=strlen(s);
            free(s);
        }
        free(sep);
        return valStrOwn(out);
    }
    if(id==B_replace){
        // replace(teks, cari, ganti): ganti semua yang ketemu
        needArgs(name,n,3);
        char *s=valToStr(a[0]), *from=valToStr(a[1]), *to=valToStr(a[2]);
        size_t fl=strlen(from), tl=strlen(to);
        if(fl==0) nrxFail("replace: teks yang dicari tidak boleh kosong");
        size_t cap=strlen(s)+1, len=0;
        char *out=malloc(cap);
        char *p=s, *hit;
        while((hit=strstr(p,from))){
            size_t chunk=hit-p;
            if(len+chunk+tl+1>cap){
                cap=(len+chunk+tl+1)*2;
                out=realloc(out,cap);
            }
            memcpy(out+len,p,chunk);
            len+=chunk;
            memcpy(out+len,to,tl);
            len+=tl;
            p=hit+fl;
        }
        size_t rest=strlen(p);
        if(len+rest+1>cap) out=realloc(out,len+rest+1);
        memcpy(out+len,p,rest+1);
        free(s);
        free(from);
        free(to);
        return valStrOwn(out);
    }
    if(id==B_contains){
        // contains(teks, bagian), contains(list, nilai), atau contains(map, key); sama dengan operator in
        needArgs(name,n,2);
        return valBool(containsValue(a[0],a[1]));
    }
    if(id==B_int || id==B_float){
        needArgs(name,n,1);
        double d;
        if(!numLike(a[0],&d)){
            char *s=valToStr(a[0]);
            nrxFail("%s(): \"%s\" bukan angka",name,s);
        }
        return name[0]=='i' ? valNum(trunc(d)) : valDec(d);
    }
    if(id==B_time || id==B_date){
        needArgs(name,n,0);
        time_t t=time(NULL);
        char b[32];
        strftime(b,sizeof b,name[0]=='t' ? "%H:%M:%S" : "%Y-%m-%d",localtime(&t));
        return valStr(b);
    }
    if(id==B_timer){
        needArgs(name,n,0);
        return valNum((double)clock()/CLOCKS_PER_SEC);
    }
    if(id==B_sort){
        // hasilnya list baru yang terurut (angka dari kecil, teks sesuai abjad); list asli tidak berubah
        needArgs(name,n,1);
        List *src=needList(name,a[0]);
        List *l=listNew();
        for(int i=0;i<src->count;i++) listPush(l,valCopy(src->items[i]));
        if(l->count>1) qsort(l->items,l->count,sizeof(Value),sortCmp);
        return valList(l);
    }
    if(id==B_array){
        // array(n) / array(n, nilai): n angka; array(list): isi list angka jadi array
        if(n<1 || n>2) nrxFail("array butuh 1 atau 2 argumen: array(10), array(10, 1.5), atau array([1, 2])");
        if(a[0].type==T_LIST){
            if(n!=1) nrxFail("array butuh 1 atau 2 argumen: array(10), array(10, 1.5), atau array([1, 2])");
            List *l=a[0].list;
            NumArr *r=arrNew(l->count);
            for(int i=0;i<l->count;i++){
                if(l->items[i].type!=T_NUM) nrxFail("isi array hanya angka, tidak bisa diisi %s",valKindName(l->items[i]));
                r->d[i]=l->items[i].num;
            }
            return valArr(r);
        }
        double d, fill=0;
        if(!numLike(a[0],&d) || d<0 || d!=(long long)d) nrxFail("array butuh jumlah bilangan bulat >= 0: array(10)");
        if(d>100000000) nrxFail("array: jumlah terlalu besar (maksimal 100 juta)");
        if(n==2){
            if(a[1].type!=T_NUM) nrxFail("isi array hanya angka, tidak bisa diisi %s",valKindName(a[1]));
            fill=a[1].num;
        }
        NumArr *r=arrNew((int)d);
        if(fill!=0) for(int i=0;i<r->count;i++) r->d[i]=fill;
        return valArr(r);
    }
    if(id==B_filled){
        // filled(1000, 0.0): list berisi n nilai yang sama, dibuat sekaligus
        needArgs(name,n,2);
        double d;
        if(!numLike(a[0],&d) || d<0 || d!=(long long)d) nrxFail("filled butuh jumlah bilangan bulat >= 0: filled(10, 0)");
        if(d>10000000) nrxFail("filled: jumlah terlalu besar (maksimal 10 juta)");
        int cnt=(int)d;
        List *l=listNew();
        l->items=malloc(sizeof(Value)*(cnt>0 ? cnt : 1));
        l->cap=cnt>0 ? cnt : 1;
        for(int i=0;i<cnt;i++) l->items[i]=valCopy(a[1]);
        l->count=cnt;
        l->elem=valKind(a[1]);
        if(l->elem==K_STRUCT) l->ename=a[1].map->structName;
        return valList(l);
    }
    if(id==B_reverse){
        needArgs(name,n,1);
        if(a[0].type==T_LIST){
            List *src=a[0].list, *l=listNew();
            for(int i=src->count-1;i>=0;i--) listPush(l,valCopy(src->items[i]));
            return valList(l);
        }
        char *s=valToStr(a[0]);
        int len=strlen(s);
        for(int i=0;i<len/2;i++){
            char t=s[i];
            s[i]=s[len-1-i];
            s[len-1-i]=t;
        }
        return valStrOwn(s);
    }
    if(id==B_indexOf){
        // posisi pertama nilai di list (atau bagian di teks); -1 kalau tidak ada
        needArgs(name,n,2);
        if(a[0].type==T_LIST){
            for(int i=0;i<a[0].list->count;i++){
                if(compareVals(a[0].list->items[i],a[1])==0) return valNum(i);
            }
            return valNum(-1);
        }
        char *s=valToStr(a[0]), *sub=valToStr(a[1]);
        char *hit=strstr(s,sub);
        Value r=valNum(hit ? hit-s : -1);
        free(s);
        free(sub);
        return r;
    }
    if(id==B_substr){
        // substr(teks, mulai, panjang): substr("Halo Dunia", 0, 4) -> "Halo"
        if(n!=2 && n!=3) nrxFail("substr butuh 2 atau 3 argumen: substr(teks, mulai, panjang)");
        char *s=valToStr(a[0]);
        int len=strlen(s);
        int from=clampIndex(needNum(name,a[1]),len);
        int count=n==3 ? (int)needNum(name,a[2]) : len-from;
        if(count<0) count=0;
        if(from+count>len) count=len-from;
        Value r=valStr("");
        free(r.str);
        r.str=malloc(count+1);
        memcpy(r.str,s+from,count);
        r.str[count]=0;
        free(s);
        return r;
    }
    if(id==B_slice){
        // slice(list atau teks, awal, akhir): ambil dari awal sampai sebelum akhir; negatif = dari belakang
        if(n!=2 && n!=3) nrxFail("slice butuh 2 atau 3 argumen: slice(list, awal, akhir)");
        int isList=a[0].type==T_LIST;
        char *s=isList ? NULL : valToStr(a[0]);
        int len=isList ? a[0].list->count : (int)strlen(s);
        int from=clampIndex(needNum(name,a[1]),len);
        int to=n==3 ? clampIndex(needNum(name,a[2]),len) : len;
        if(to<from) to=from;
        if(isList){
            List *l=listNew();
            for(int i=from;i<to;i++) listPush(l,valCopy(a[0].list->items[i]));
            return valList(l);
        }
        Value r=valStr("");
        free(r.str);
        r.str=malloc(to-from+1);
        memcpy(r.str,s+from,to-from);
        r.str[to-from]=0;
        free(s);
        return r;
    }
    if(id==B_typeOf){
        // "int", "double", "string", "bool", "list", "map", atau nama struct
        needArgs(name,n,1);
        if(a[0].type==T_NUM) return valStr(a[0].sub ? "double" : "int");
        if(a[0].type==T_LIST) return valStr("list");
        if(a[0].type==T_ARR) return valStr("array");
        if(a[0].type==T_MAP) return valStr(a[0].map->structName ? a[0].map->structName : "map");
        return valStr(a[0].sub ? "bool" : "string");
    }
    if(id==B_exit){
        // exit() atau exit(kode): hentikan program saat itu juga
        if(n>1) nrxFail("exit butuh 0 atau 1 argumen: exit() atau exit(kode)");
        fflush(stdout);
        exit(n==1 ? (int)needNum(name,a[0]) : 0);
    }
    if(nrxKeyboardOwns(name)){
        if(!keyboardLib) nrxFail("%s() butuh library keyboard, tambahkan di atas: lib nrx_keyboard.h",name);
        return nrxKeyboardCall(name,a,n);
    }
    if(nrxWindowFuncOwns(name)){
        if(!windowLib) nrxFail("%s() butuh library window, tambahkan di atas: lib nrx_window.h",name);
        return nrxWindowCall(name,a,n);
    }
    if(nrxHttpOwns(name)){
        if(!httpLib) nrxFail("%s() butuh library HTTP, tambahkan di atas: lib nrx_http.h",name);
        return nrxHttpCall(name,a,n);
    }
    if(nrx3dOwns(name)){
        if(!threeDLib) nrxFail("%s() butuh library 3D, tambahkan di atas: lib nrx_3d.h",name);
        if(!nrx3dCallPtr) nrxFail("lib nrx_3d.h tidak ikut di program ini");
        return nrx3dCallPtr(name,a,n);
    }
    if(nrxSoundOwns(name)){
        if(!soundLib) nrxFail("%s() butuh library suara, tambahkan di atas: lib nrx_sound.h",name);
        return nrxSoundCall(name,a,n);
    }
    if(nrxJsonOwns(name)){
        if(!jsonLib) nrxFail("%s() butuh library JSON, tambahkan di atas: lib nrx_json.h",name);
        return nrxJsonCall(name,a,n);
    }
    if(nrxConsoleOwns(name)){
        if(!consoleLib) nrxFail("%s() butuh library console, tambahkan di atas: lib nrx_console.h",name);
        return nrxConsoleCall(name,a,n);
    }
    if(nrxRandOwns(name)){
        if(!randLib) nrxFail("%s() butuh library random, tambahkan di atas: lib nrx_rand.h",name);
        return nrxRandCall(name,a,n);
    }
    if(nrxFileOwns(name)){
        if(!fileLib) nrxFail("%s() butuh library file, tambahkan di atas: lib nrx_crudfile.h",name);
        return nrxFileCall(name,a,n);
    }
    StructDef *hiddenStruct=NULL;
    StructDef *sd=findStruct(name,&hiddenStruct);
    if(sd) return newInstance(sd,a,n);
    int hiddenPc=-1;
    int fpc=findFuncVisible(name,&hiddenPc);
    if(fpc>=0) return callFunc(fpc,a,n);
    if(hiddenPc>=0 && funcAt(hiddenPc)->exported) notImported(name,funcAt(hiddenPc)->file);
    if(hiddenStruct && hiddenStruct->exported) notImported(name,hiddenStruct->file);
    if(hiddenPc>=0) nrxFail("fungsi %s ada di %s tapi tidak di-export (tulis di sana: ex func %s)",name,files[lineFile[hiddenPc]],name);
    if(hiddenStruct) nrxFail("struct %s ada di %s tapi tidak di-export (tulis di sana: ex struct %s)",name,files[hiddenStruct->file],name);
    nrxFail("fungsi %s tidak ditemukan",name);
    return valStr("");
}

static Value callNamed(const char *name, Value *a, int n){
    return callNamedId(builtinId(name),name,a,n);
}

// ---------------- interpreter ----------------

#define RC_OK 0
#define RC_RETURN 1
#define RC_BREAK 2
#define RC_CONTINUE 3

static Value retVal;
static int execRange(int start, int stop);
// isi fungsi yang sudah di-compile ke C (nrxlang build), per baris "func"; NULL = dijalankan interpreter
static int (*compiledBody[MAX_LINES])(void);
// nilai kanan be yang sudah dihitung kode hasil compile; NULL = execBe menghitungnya sendiri
static Value *bePreset;

// break/continue yang sampai ke luar fungsi atau program berarti tidak ada loop di sekitarnya
static void noLoop(int rc){
    nrxFail("%s hanya boleh di dalam loop",rc==RC_BREAK ? "break" : "continue");
}

static int isType(const char *t);
// nama type isi dari resolveType terakhir ("list Player" -> "Player")
static const char *resolvedElemName;

// "int", "list int", "map string", atau nama struct -> K_...; teks kosong = tanpa tipe.
// 0 kalau tipenya tidak dikenal
static int resolveType(const char *txt, int *kind, int *elem, const char **sname){
    *kind=K_ANY;
    *elem=K_ANY;
    *sname=NULL;
    if(!txt[0]) return 1;
    char a[48]="", b[48]="", c[48]="";
    sscanf(txt,"%47s %47s %47s",a,b,c);
    if(c[0]) return 0;
    if(isType(a)){
        *kind=kindFromName(a);
        if(!b[0]) return 1;
        if(*kind!=K_LIST && *kind!=K_MAP) return 0;
        resolvedElemName=NULL;
        if(isType(b)) *elem=kindFromName(b);
        else if(findStruct(b,NULL)){
            *elem=K_STRUCT;
            resolvedElemName=findStruct(b,NULL)->name;
        }
        else return 0;
        return 1;
    }
    StructDef *s=findStruct(a,NULL);
    if(!s || b[0]) return 0;
    *kind=K_STRUCT;
    *sname=s->name;
    return 1;
}

// isi parameter ke-i: kalau tipenya ditulis, argumennya harus cocok
static void bindParam(Var *pv, Func *f, int i, Value v){
    snprintf(pv->name,sizeof pv->name,"%s",f->params[i]);
    pv->locked=0;
    if(f->pkind[i]==K_ANY){
        takeKind(pv,v);
    } else {
        int r=fitKind(f->pkind[i],f->psname[i],f->pelem[i],&v);
        if(r==1) nrxFail("parameter %s fungsi %s bertipe int, tidak bisa diisi angka desimal; pakai int(...)",f->params[i],f->name);
        if(r==2) nrxFail("parameter %s fungsi %s bertipe %s, tidak bisa diisi %s",f->params[i],f->name,f->ptype[i],valKindName(v));
        pv->kind=f->pkind[i];
        pv->elem=f->pelem[i];
        pv->sname=f->psname[i];
    }
    pv->val=v;
}

// own: objek pemilik method (NULL untuk fungsi biasa); di dalam method terbaca sebagai variabel own
static Value callFuncOwn(int fpc, Value *own, Value *args, int nargs){
    Func *f=funcAt(fpc);
    if(nargs<f->nrequired || nargs>f->nparams){
        if(f->nrequired==f->nparams) nrxFail("fungsi %s butuh %d argumen, tapi diberi %d",f->name,f->nparams,nargs);
        nrxFail("fungsi %s butuh %d sampai %d argumen, tapi diberi %d",f->name,f->nrequired,f->nparams,nargs);
    }
    if(depth>=200) nrxFail("fungsi memanggil dirinya terlalu dalam");
    Frame *fr=&frames[depth];
    fr->count=0;
    if(own){
        Var *o=&fr->vars[fr->count++];
        snprintf(o->name,sizeof o->name,"own");
        o->val=valCopy(*own);
        o->locked=0;
        takeKind(o,*own);
    }
    int base=fr->count;
    for(int i=0;i<nargs;i++){
        bindParam(&fr->vars[base+i],f,i,valCopy(args[i]));
        fr->count++;
    }
    depth++;
    int savedPc=curPc;
    // parameter yang tidak diisi memakai nilai bawaannya (boleh memakai parameter sebelumnya)
    curPc=fpc;
    for(int i=nargs;i<f->nparams;i++){
        bindParam(&fr->vars[base+i],f,i,evalExpr(f->defaults[i]));
        fr->count++;
    }
    int rc=compiledBody[fpc] ? compiledBody[fpc]() : execRange(fpc+1,skipBlock(fpc));
    if(rc==RC_BREAK || rc==RC_CONTINUE) noLoop(rc);
    curPc=savedPc;
    depth--;
    for(int i=0;i<fr->count;i++) valFree(&fr->vars[i].val);
    Value r=valStr("");
    if(rc==RC_RETURN){
        valFree(&r);
        r=retVal;
        retVal=valStr("");
    }
    // tipe hasil yang ditulis: nilai return harus cocok
    if(f->rkind!=K_ANY){
        if(rc!=RC_RETURN) nrxFail("fungsi %s harus mengembalikan %s, tapi tidak ada return",f->name,f->rtype);
        int k=fitKind(f->rkind,f->rsname,f->relem,&r);
        if(k==1) nrxFail("fungsi %s harus mengembalikan int, bukan angka desimal; pakai int(...)",f->name);
        if(k==2) nrxFail("fungsi %s harus mengembalikan %s, bukan %s",f->name,f->rtype,valKindName(r));
    }
    return r;
}

Value callFunc(int fpc, Value *args, int nargs){
    return callFuncOwn(fpc,NULL,args,nargs);
}

static int structIndex(const char *name){
    for(int i=0;i<structCount;i++) if(!strcmp(structs[i].name,name)) return i;
    return -1;
}

// method nama milik type ke-owner; -1 kalau tidak ada
static int findMethod(int owner, const char *name){
    for(int i=0;i<funcCount;i++){
        if(funcs[i].ownerType==owner && !strcmp(funcs[i].name,name)) return funcs[i].pc;
    }
    return -1;
}

static int instDepth=0;

// nilai kosong sesuai tipe: 0, 0.0, false, "", list/map kosong, atau objek type dengan nilai bawaan
static Value defaultFor(const char *type);
static Value defaultKind(int kind, int elem, const char *ename, const char *sname, const char *field){
    if(kind==K_INT) return valNum(0);
    if(kind==K_DOUBLE || kind==K_FLOAT) return valDec(0);
    if(kind==K_BOOL) return valBool(0);
    if(kind==K_MIXED) return defaultFor("json");
    if(kind==K_LIST){
        List *l=listNew();
        l->elem=elem;
        l->ename=ename;
        return valList(l);
    }
    if(kind==K_MAP){
        Map *m=mapNew();
        m->elem=elem;
        m->ename=ename;
        return valMap(m);
    }
    if(kind==K_STRUCT){
        StructDef *sd=&structs[structIndex(sname)];
        if(instDepth>8 || sd->initPc>=0) nrxFail("field %s butuh nilai bawaan, contoh: %s %s = %s(...)",field,field,sname,sname);
        return newInstance(sd,NULL,0);
    }
    return valStr("");
}

// Player(...): field diisi nilai bawaannya, lalu init dijalankan (kalau ada);
// tanpa init, argumen mengisi field sesuai urutan
static Value newInstance(StructDef *sd, Value *a, int n){
    Map *m=mapNew();
    m->structName=sd->name;
    instDepth++;
    int savedPc=curPc;
    for(int i=0;i<sd->nfields;i++){
        Value v;
        if(sd->fdef[i][0]){
            curPc=sd->pc;
            v=evalExpr(sd->fdef[i]);
            curPc=savedPc;
            int r=fitKind(sd->fkind[i],sd->fsname[i],sd->felem[i],&v);
            if(r) nrxFailAt(sd->pc,"field %s di type %s bertipe %s, nilai bawaannya tidak cocok (%s)",sd->fields[i],sd->name,sd->ftype[i],valKindName(v));
        } else {
            v=defaultKind(sd->fkind[i],sd->felem[i],sd->fename[i],sd->fsname[i],sd->fields[i]);
        }
        mapSet(m,sd->fields[i],v);
    }
    instDepth--;
    Value obj=valMap(m);
    if(sd->initPc>=0){
        Func *init=funcAt(sd->initPc);
        if(n<init->nrequired || n>init->nparams) nrxFail("%s butuh %d argumen, tapi diberi %d",sd->name,init->nparams,n);
        Value r=callFuncOwn(sd->initPc,&obj,a,n);
        valFree(&r);
        return obj;
    }
    if(n>sd->nfields) nrxFail("%s punya %d field, tapi diberi %d nilai",sd->name,sd->nfields,n);
    for(int i=0;i<n;i++){
        Value v=valCopy(a[i]);
        int r=fitKind(sd->fkind[i],sd->fsname[i],sd->felem[i],&v);
        if(r==1) nrxFail("field %s di type %s bertipe int, tidak bisa diisi angka desimal; pakai int(...)",sd->fields[i],sd->name);
        if(r==2) nrxFail("field %s di type %s bertipe %s, tidak bisa diisi %s",sd->fields[i],sd->name,sd->ftype[i],valKindName(v));
        Value *slot=mapGet(m,sd->fields[i]);
        valFree(slot);
        *slot=v;
    }
    return obj;
}

// obj.nama(args): jalankan method milik type obj
static Value callMethod(Value obj, const char *name, Value *a, int n){
    int owner=structIndex(obj.map->structName);
    int pc=findMethod(owner,name);
    if(pc<0) nrxFail("type %s tidak punya method %s",obj.map->structName,name);
    return callFuncOwn(pc,&obj,a,n);
}

static char *readIdent(char *p, char *out, int size){
    int k=0;
    while(idChar(*p)){
        if(k<size-1) out[k++]=*p;
        p++;
    }
    out[k]=0;
    return p;
}

static char *skipWs(char *p){
    while(*p==' '||*p=='\t') p++;
    return p;
}

static int isType(const char *t){
    return !strcmp(t,"int") || !strcmp(t,"float") || !strcmp(t,"double")
        || !strcmp(t,"bool") || !strcmp(t,"string") || !strcmp(t,"list") || !strcmp(t,"map")
        || !strcmp(t,"json") || !strcmp(t,"array");
}

static Value defaultFor(const char *type){
    if(!strcmp(type,"int") || !strcmp(type,"float") || !strcmp(type,"double")) return valNum(0);
    if(!strcmp(type,"bool")) return valBool(0);
    if(!strcmp(type,"list")) return valList(listNew());
    if(!strcmp(type,"map")) return valMap(mapNew());
    if(!strcmp(type,"array")) return valArr(arrNew(0));
    if(!strcmp(type,"json")){
        // json kosong: map yang isinya boleh campur
        Map *m=mapNew();
        m->elem=K_MIXED;
        return valMap(m);
    }
    return valStr("");
}

// satu langkah ke dalam: base[idx] untuk list/map. key map yang belum ada dibuat (nilai awal 0, berguna
// untuk +=) hanya di langkah terakhir; di tengah jalan key harus sudah ada
// diisi stepInto di langkah terakhir: list/map tempat nilai disimpan, dan apakah key-nya baru dibuat
static Value *lastBase;
static int lastCreated;

// list/map yang di-lock tetap terkunci walaupun dipegang variabel lain
static void stepLockCheck(Value *base, const char *path){
    if((base->type==T_LIST && base->list->locked) || (base->type==T_MAP && base->map->locked)
       || (base->type==T_ARR && base->arr->locked)){
        nrxFail("isi %s di-lock, tidak bisa diubah",path);
    }
}

static Value *stepIdx(Value *base, Value idx, int last, const char *path);
static Value arrSlot;      // isi array yang sedang diubah (lihat stepIdx)
static int arrSlotIdx;

static Value *stepInto(Value *base, const char *idxSrc, int last, const char *path){
    stepLockCheck(base,path);
    return stepIdx(base,evalExpr(idxSrc),last,path);
}

// idx dibebaskan
static Value *stepIdx(Value *base, Value idx, int last, const char *path){
    Value *r=NULL;
    if(base->type==T_MAP){
        char *key=valToStr(idx);
        r=mapGet(base->map,key);
        if(!r && base->map->structName) nrxFail("%s tidak punya field %s",base->map->structName,key);
        if(!r && !last) nrxFail("key \"%s\" tidak ada di %s",key,path);
        if(last) lastCreated=!r;
        if(!r){
            mapElem(base->map);
            mapSet(base->map,key,valNum(0));
            r=mapGet(base->map,key);
        }
        free(key);
    } else if(base->type==T_LIST){
        r=&base->list->items[needIndex(base->list,idx)];
        if(last) lastCreated=0;
    } else if(base->type==T_ARR){
        // isi array bukan Value: nilainya disalin ke arrSlot, storeInto menulis balik
        if(idx.type!=T_NUM) nrxFail("index harus angka");
        arrSlotIdx=arrIndex(base->arr,idx.num);
        if(!last) nrxFail("isi %s hanya angka, tidak punya isi lagi",path);
        arrSlot=valDec(base->arr->d[arrSlotIdx]);
        r=&arrSlot;
        lastCreated=0;
    } else {
        nrxFail("%s bukan list atau map",path);
    }
    if(last) lastBase=base;
    valFree(&idx);
    return r;
}

// tempat nilai name + rantai akses, mis. data.pemain[0].hp
static Value *targetRef(const char *name, char acc[][256], int nacc){
    Var *v=findVarPtr(name);
    if(!v) unknownVar(name);
    if(v->locked) nrxFail("%s di-lock, isinya tidak bisa diubah",name);
    Value *cur=&v->val;
    for(int i=0;i<nacc;i++) cur=stepInto(cur,acc[i],i==nacc-1,name);
    return cur;
}

// be nama tipe = nilai: variabel baru langsung memakai tipe yang ditulis
static void declareVar(const char *name, int kind, int elem, Value v){
    Var *p=findVarPtr(name);
    if(p && p->locked) nrxFail("%s di-lock, nilainya tidak bisa diubah",name);
    if(!p){
        if(depth>0) p=newVar(frames[depth-1].vars,&frames[depth-1].count,64,name);
        else p=newVar(globals,&globalCount,1024,name);
    }
    if(p->kind==K_ANY){
        p->kind=kind;
        p->elem=elem;
        p->sname=NULL;
    } else if(p->elem==K_ANY){
        p->elem=elem;
    }
    checkVarKind(p,&v);
    valFree(&p->val);
    p->val=v;
}

// be list[i] = v / be map[key] = v / be p.field = v: jenisnya harus sama dengan isi yang lain
// (untuk struct: sama dengan nilai field itu sebelumnya)
static void storeInto(Value *base, Value *t, int created, Value v, const char *name){
    if(base->type==T_ARR){
        if(v.type!=T_NUM) nrxFail("isi array hanya angka, tidak bisa diisi %s",valKindName(v));
        base->arr->d[arrSlotIdx]=v.num;
        valFree(&v);
        return;
    }
    if(base->type==T_LIST){
        listElem(base->list);
        fitElem(&base->list->elem,&base->list->ename,&v,name);
    } else if(base->map->structName){
        // field type: jenisnya tetap seperti yang ditulis di type
        const char *field=base->map->keys[t-base->map->vals];
        int r=fitKind(valKind(*t),valKind(*t)==K_STRUCT ? t->map->structName : NULL,K_ANY,&v);
        if(r==1) nrxFail("field %s di type %s bertipe int, tidak bisa diisi angka desimal; pakai int(...)",field,base->map->structName);
        if(r==2) nrxFail("field %s di type %s bertipe %s, tidak bisa diisi %s",field,base->map->structName,valKindName(*t),valKindName(v));
    } else {
        Map *m=base->map;
        // key yang baru dibuat di map kosong belum menentukan jenis isi
        if(!(created && m->count==1)) mapElem(m);
        fitElem(&m->elem,&m->ename,&v,name);
    }
    valFree(t);
    *t=v;
}

// ask "prompt" / ask.int "prompt"; prompt boleh berisi variabel: ask "umur " nama "? "
static Value askValue(char *p, int intOnly){
    char *rest=p+(startsWord(p,"ask.int") ? 7 : 3);
    char *q=evalSeqStr(rest);
    char buf[256];
    readInput(buf,sizeof buf,q,intOnly);
    free(q);
    return intOnly ? valNum(atof(buf)) : valStr(buf);
}

// hasil membaca teks "be ..." (tidak bergantung pada nilai variabel); disimpan per baris
// supaya baris yang jalan berulang (di dalam loop) tidak dibaca ulang setiap kali
typedef struct {
    char name[64], type[32];
    char (*acc)[256];      // rantai [index] / .field, sebagai teks ekspresi
    int nacc, kind, elem;
    char op;               // 0 (be nama tipe), '=', atau + - * / %
    const char *rhs;       // teks nilai di kanan
    int ask;               // 1 = ask, 2 = ask.int
    int emptyLit;          // nilainya [] atau {}
    int off;               // posisi teks di barisnya (be di dalam "ex be ...")
} BeParse;
static BeParse *beCache[MAX_LINES];

static void parseBe(char *text, BeParse *b){
    char *p=skipWs(text+2);
    char *name=b->name, *type=b->type;
    type[0]=0;
    char acc[16][256];
    int nacc=0;
    if(!idStart(*p)) nrxFail("nama variabel tidak valid: %s",text);
    p=readIdent(p,name,sizeof b->name);
    while(*p=='[' || (*p=='.' && idStart(p[1]))){
        if(nacc>=16) nrxFail("terlalu banyak tingkat [ ] atau . (maksimal 16)");
        if(*p=='['){
            int d=0;
            char *q=p;
            for(; *q; q++){
                if(*q=='[') d++;
                else if(*q==']' && --d==0) break;
            }
            if(!*q) nrxFail("kurang tanda ]");
            snprintf(acc[nacc],sizeof acc[nacc],"%.*s",(int)(q-p-1),p+1);
            p=q+1;
        } else {
            // .hp sama dengan ["hp"]
            char field[64];
            p=readIdent(p+1,field,sizeof field);
            snprintf(acc[nacc],sizeof acc[nacc],"\"%s\"",field);
        }
        nacc++;
    }
    p=skipWs(p);
    // be daftar list int: list dan map boleh diikuti jenis isinya
    char elemType[32]="";
    if(idStart(*p)){
        p=readIdent(p,type,sizeof b->type);
        if(!isType(type)) nrxFail("tipe %s tidak dikenal, pakai: int, float, double, bool, string, list, map, json",type);
        p=skipWs(p);
        if((!strcmp(type,"list") || !strcmp(type,"map")) && idStart(*p)){
            p=readIdent(p,elemType,sizeof elemType);
            if(!isType(elemType)) nrxFail("jenis isi %s tidak dikenal, pakai: int, float, double, bool, string, list, map, json",elemType);
            p=skipWs(p);
        }
    }
    int kind=type[0] ? kindFromName(type) : K_ANY;
    int elem=elemType[0] ? kindFromName(elemType) : K_ANY;
    if(type[0] && nacc) nrxFail("cara menulis: be nama[index] = nilai");
    char op=0;
    if(*p=='=' && p[1]!='='){
        op='=';
        p++;
    } else if(*p && strchr("+-*/%",*p) && p[1]=='='){
        op=*p;
        p+=2;
    } else if(*p){
        nrxFail("cara menulis: be nama = nilai");
    }
    if(!op && nacc) nrxFail("cara menulis: be nama[index] = nilai");
    if(op){
        p=skipWs(p);
        if(!*p) nrxFail("nilai setelah = masih kosong");
    }
    b->nacc=nacc;
    b->acc=nacc ? malloc(sizeof(*b->acc)*nacc) : NULL;
    memcpy(b->acc,acc,sizeof(*b->acc)*nacc);
    b->kind=kind;
    b->elem=elem;
    b->op=op;
    b->rhs=p;
    b->ask=op=='=' ? (startsWord(p,"ask.int") ? 2 : startsWord(p,"ask") ? 1 : 0) : 0;
    b->emptyLit=!strcmp(p,"[]") || !strcmp(p,"{}");
}

// be nama = nilai | be nama tipe = nilai | be nama tipe | be nama += nilai
// be nama[i] = nilai | be nama.field = nilai | be data.pemain[0].hp = nilai
static void execBe(char *text){
    // diambil paling awal supaya tidak tertinggal kalau baris ini error
    Value *pre=bePreset;
    bePreset=NULL;
    // teks dari lines[]: hasil bacanya disimpan; teks lain (mis. dari lock) dibaca tiap kali
    BeParse tmp, *b=&tmp;
    int line=-1, off=0;
    if(text>=lines[0] && text<lines[0]+sizeof lines){
        line=(int)((text-lines[0])/sizeof lines[0]);
        off=(int)((text-lines[0])%sizeof lines[0]);
        if(beCache[line] && beCache[line]->off==off) b=beCache[line];
        else line=line*-1-2;
    }
    if(b==&tmp){
        parseBe(text,b);
        if(line<=-2){
            b=malloc(sizeof *b);
            *b=tmp;
            b->off=off;
            beCache[-line-2]=b;
        }
    }
    const char *name=b->name, *type=b->type;
    char (*acc)[256]=b->acc;
    int nacc=b->nacc, kind=b->kind, elem=b->elem;
    char op=b->op;
    const char *p=b->rhs;
    if(type[0]){
        Var *x=findVarPtr(name);
        if(x && x->kind!=K_ANY && (x->kind!=kind || (elem!=K_ANY && x->elem!=K_ANY && x->elem!=elem))){
            nrxFail("%s sudah bertipe %s, tidak bisa diganti jadi %s",name,x->kind==K_STRUCT ? x->sname : kindName(x->kind),type);
        }
    }
    if(!op){
        // deklarasi memori: be <nama> <type>, isi dengan nilai default
        declareVar(name,kind,elem,defaultFor(type));
        return;
    }
    // [] dan {} tidak memberi tahu jenis isinya
    if(op=='=' && !nacc && elem==K_ANY && b->emptyLit){
        Var *x=findVarPtr(name);
        if(!x || x->elem==K_ANY){
            if(p[0]=='[') nrxFail("list kosong harus ditulis jenis isinya: be %s list int",name);
            nrxFail("map kosong harus ditulis jenis isinya: be %s map int",name);
        }
    }
    Value v;
    if(b->ask){
        // be x int = ask juga otomatis hanya menerima integer; float/double menerima angka apa saja
        v=askValue((char*)p,b->ask==2 || kind==K_INT);
        if((kind==K_DOUBLE || kind==K_FLOAT) && v.type==T_STR){
            double d;
            if(!numLike(v,&d)) nrxFail("%s bertipe %s, tidak boleh berisi \"%s\"",name,type,v.str);
            valFree(&v);
            v=valDec(d);
        }
    } else if(pre){
        v=*pre;
    } else {
        v=evalSeq(p);
    }
    int created=0;
    if(op!='='){
        Value cur;
        if(nacc){
            cur=valCopy(*targetRef(name,acc,nacc));
            created=lastCreated;
            if(created){
                // key baru: mulai dari nilai kosong jenis isi map (0, 0.0, "", false);
                // kalau jenis isinya belum diketahui, ikut jenis nilai di kanan
                int k=lastBase->map->elem;
                if(k==K_ANY || k==K_MIXED) k=valKind(v);
                if(k==K_INT || k==K_DOUBLE || k==K_FLOAT || k==K_STRING || k==K_BOOL){
                    valFree(&cur);
                    cur=defaultKind(k,K_ANY,NULL,NULL,"");
                }
            }
        } else {
            Var *x=findVarPtr(name);
            cur=x ? valCopy(x->val) : valNum(0);
        }
        Value r=binop(cur,op,v);
        valFree(&cur);
        valFree(&v);
        v=r;
    }
    if(nacc){
        Value *t=targetRef(name,acc,nacc);
        created|=lastCreated;
        storeInto(lastBase,t,created,v,name);
    } else if(kind!=K_ANY){
        declareVar(name,kind,elem,v);
    } else {
        setVar(name,v);
    }
}

// variabel dengan nama ini di tempat yang sedang jalan (lokal di dalam fungsi, global di luar)
static Var *varHere(const char *name){
    if(depth>0) return findIn(frames[depth-1].vars,frames[depth-1].count,name);
    return findGlobal(name,NULL);
}

// delete a, b: hapus variabel dan lepaskan memorinya; setelah itu namanya dianggap belum dibuat
static void execDelete(char *text){
    char *p=skipWs(text+6);
    if(!*p) nrxFail("cara menulis: delete nama  atau  delete a, b");
    for(;;){
        char name[64];
        if(!idStart(*p)) nrxFail("nama variabel tidak valid setelah delete: %s",p);
        p=skipWs(readIdent(p,name,sizeof name));
        Var *arr=globals;
        int *count=&globalCount;
        Var *v=NULL;
        if(depth>0){
            v=findIn(frames[depth-1].vars,frames[depth-1].count,name);
            if(v){
                arr=frames[depth-1].vars;
                count=&frames[depth-1].count;
            }
        }
        if(!v){
            v=findGlobal(name,NULL);
            if(!v) unknownVar(name);
            if(v->owner<0) nrxFail("%s variabel bawaan, tidak bisa di-delete",name);
            if(v->owner!=curFile()) nrxFail("%s milik file %s, hanya bisa di-delete di sana",name,files[v->owner]);
        }
        if(v->locked) nrxFail("%s di-lock, tidak bisa di-delete",name);
        valFree(&v->val);
        int i=(int)(v-arr);
        memmove(&arr[i],&arr[i+1],(size_t)(*count-i-1)*sizeof *arr);
        (*count)--;
        if(!*p) break;
        if(*p!=',') nrxFail("pisahkan nama dengan koma: delete a, b");
        p=skipWs(p+1);
    }
}

// lock NAMA = nilai: variabel yang tidak bisa diubah lagi; isi list/map-nya ikut terkunci
static Var *execLock(char *text){
    char *p=skipWs(text+4);
    char name[64];
    if(!idStart(*p)) nrxFail("cara menulis: lock NAMA = nilai");
    char *after=readIdent(p,name,sizeof name);
    if(*after=='[' || *after=='.') nrxFail("lock hanya untuk membuat variabel baru: lock NAMA = nilai");
    char *eq=strchr(after,'=');
    if(!eq || strchr("+-*/%",eq[-1])) nrxFail("lock butuh nilai: lock %s = nilai",name);
    if(varHere(name)) nrxFail("variabel %s sudah ada; lock hanya untuk membuat variabel baru",name);
    char buf[520];
    snprintf(buf,sizeof buf,"be%s",text+4);
    execBe(buf);
    Var *v=varHere(name);
    v->locked=1;
    if(v->val.type==T_LIST) v->val.list->locked=1;
    if(v->val.type==T_MAP) v->val.map->locked=1;
    if(v->val.type==T_ARR) v->val.arr->locked=1;
    return v;
}

// baris yang berupa pemanggilan fungsi, mis. tambah() atau writeFile("a.txt", "x")
static int isCallStmt(const char *t){
    // nama(...) atau obj.method(...)
    if(!idStart(*t)) return 0;
    while(idChar(*t) || (*t=='.' && idStart(t[1]))) t++;
    while(*t==' ') t++;
    return *t=='(';
}

static int isElse(char *t){
    return strcmp(t,"else")==0 || strcmp(t,"else:")==0;
}

static int isElseIf(char *t){
    return strncmp(t,"else if ",8)==0;
}

// for nama in sesuatu: ambil nama variabel dan isi yang akan dijalani satu per satu
static List *forItems(char *text, char *var, int size){
    char *p=skipWs(text+3);
    if(!idStart(*p)) nrxFail("cara menulis: for nama in list");
    p=skipWs(readIdent(p,var,size));
    if(!startsWord(p,"in")) nrxFail("cara menulis: for nama in list");
    return rt_iter(evalExpr(p+2));
}

// nilai yang dijalani for jadi list; pemanggil melepas list itu setelah loop selesai
List *rt_iter(Value v){
    List *items;
    if(v.type==T_LIST){
        // referensi milik v dipegang loop sampai selesai
        return v.list;
    } else if(v.type==T_ARR){
        items=listNew();
        for(int i=0;i<v.arr->count;i++) listPush(items,valDec(v.arr->d[i]));
    } else if(v.type==T_MAP){
        // map dijalani per key
        items=listNew();
        for(int i=0;i<v.map->count;i++) listPush(items,valStr(v.map->keys[i]));
    } else if(v.type==T_STR){
        // teks dijalani per huruf
        items=listNew();
        for(char *c=v.str; *c; c++){
            char b[2]={*c,0};
            listPush(items,valStr(b));
        }
    } else {
        nrxFail("for butuh list, teks, atau map; untuk angka pakai range(), contoh: for i in range(10)");
        items=NULL;
    }
    valFree(&v);
    return items;
}

// try ... catch err: error di dalam try tidak menghentikan program, tapi menjalankan catch
static int runTry(int pc, int *nextPc){
    int indent=indents[pc];
    int catchLine=skipBlock(pc);
    if(catchLine>=lineCount || indents[catchLine]!=indent || !startsWord(lines[catchLine],"catch")){
        nrxFail("try harus diikuti catch dengan indent yang sama");
    }
    int catchEnd=skipBlock(catchLine);
    *nextPc=catchEnd;
    if(tryTop>=64) nrxFail("try bersarang terlalu dalam");
    int savedDepth=depth;
    const char *savedEp=ep;
    if(RT_SETJMP(tryBufs[tryTop++])==0){
        int rc=execRange(pc+1,catchLine);
        tryTop--;
        return rc;
    }
    // ada error: kembalikan keadaan seperti sebelum try (fungsi yang terpotong dibereskan)
    tryTop--;
    while(depth>savedDepth){
        depth--;
        for(int i=0;i<frames[depth].count;i++) valFree(&frames[depth].vars[i].val);
    }
    ep=savedEp;
    curPc=catchLine;
    char *p=skipWs(lines[catchLine]+5);
    if(*p){
        if(!isIdent(p)) nrxFail("cara menulis: catch atau catch namaVariabel");
        setVar(p,valStr(tryMsg));
    }
    return execRange(catchLine+1,catchEnd);
}

// options nilai / option a, b / else: jalankan option pertama yang cocok, atau else kalau tidak ada yang cocok
static int runOptions(int pc, int *nextPc){
    int end=skipBlock(pc);
    *nextPc=end;
    if(end==pc+1) return RC_OK;
    Value v=evalExpr(lines[pc]+7);
    int childIndent=indents[pc+1];
    int elseLine=-1, chosen=-1;
    for(int j=pc+1; j<end; j=skipBlock(j)){
        curPc=j;
        if(indents[j]!=childIndent) nrxFail("isi options harus sejajar (indent sama)");
        if(isElse(lines[j])){
            elseLine=j;
            continue;
        }
        if(!startsWord(lines[j],"option")) nrxFail("di dalam options hanya boleh option atau else");
        if(chosen>=0) continue;
        // option "1", "2" -> dicek sebagai list ["1", "2"]
        char *src=lines[j]+6;
        char *buf=malloc(strlen(src)+3);
        sprintf(buf,"[%s]",src);
        Value vals=evalExpr(buf);
        free(buf);
        if(vals.list->count==0) nrxFail("option butuh nilai, contoh: option \"1\"");
        for(int k=0;k<vals.list->count;k++){
            if(compareVals(vals.list->items[k],v)==0){
                chosen=j;
                break;
            }
        }
    }
    valFree(&v);
    if(chosen<0) chosen=elseLine;
    if(chosen<0) return RC_OK;
    return execRange(chosen+1,skipBlock(chosen));
}

// ---- pohon perintah ----
// tiap baris dibaca sekali jadi node perintah: jenisnya dan akhir bloknya disimpan,
// ekspresinya sudah jadi pohon lewat cache AST. Blok (isi loop, if, fungsi) dijalankan
// sebagai blok anak; break/continue/return naik lewat kode hasil, sama seperti nanti di C.
enum {
    S_NONE, S_BE, S_LOCK, S_SKIP, S_ERRLANG, S_DELETE, S_EX, S_PRINT, S_ASK, S_WAIT,
    S_FFI, S_LIB, S_RETURN, S_BREAK, S_CONTINUE, S_THROW, S_TRY, S_CATCH, S_OPTIONS,
    S_OPTION, S_LOOP, S_FOR, S_IF, S_ELSE, S_WINDOW, S_CALL, S_UNKNOWN
};
static unsigned char stmtKind[MAX_LINES];
static int stmtEnd[MAX_LINES];

// index baris pertama setelah blok baris pc (disimpan setelah dihitung sekali)
static int blockEnd(int pc){
    if(!stmtEnd[pc]) stmtEnd[pc]=skipBlock(pc);
    return stmtEnd[pc];
}

static int classify(int pc){
    char *text=lines[pc];
    if(startsWord(text,"be")) return S_BE;
    if(startsWord(text,"lock")) return S_LOCK;
    if(startsWord(text,"type") || startsWord(text,"func")) return S_SKIP;
    if(startsWord(text,"errlang")) return S_ERRLANG;
    if(startsWord(text,"delete")) return S_DELETE;
    if(startsWord(text,"ex")) return S_EX;
    if(startsWord(text,"print")) return S_PRINT;
    if(startsWord(text,"ask.int") || startsWord(text,"ask")) return S_ASK;
    if(strncmp(text,"wait",4)==0 && (text[4]=='(' || text[4]=='.' || text[4]==' ')) return S_WAIT;
    if(startsWord(text,"ffi")) return S_FFI;
    if(startsWord(text,"lib")) return S_LIB;
    if(startsWord(text,"return")) return S_RETURN;
    if(strcmp(text,"break")==0) return S_BREAK;
    if(strcmp(text,"continue")==0) return S_CONTINUE;
    if(startsWord(text,"throw")) return S_THROW;
    if(startsWord(text,"try")) return S_TRY;
    if(startsWord(text,"catch")) return S_CATCH;
    if(startsWord(text,"options")) return S_OPTIONS;
    if(startsWord(text,"option")) return S_OPTION;
    if(startsWord(text,"loop")) return S_LOOP;
    if(startsWord(text,"for")) return S_FOR;
    if(startsWord(text,"if")) return S_IF;
    if(isElse(text) || isElseIf(text)) return S_ELSE;
    if(nrxWindowOwns(text)) return S_WINDOW;
    if(isCallStmt(text)) return S_CALL;
    return S_UNKNOWN;
}

// cabang else if / else berikutnya dalam rantai if, atau -1
static int nextBranch(int cur, int stop){
    int next=blockEnd(cur);
    if(next<stop && indents[next]==indents[cur] && (isElseIf(lines[next]) || isElse(lines[next]))) return next;
    return -1;
}

static int execRange(int start, int stop){
    for(int pc=start; pc<stop; ){
        curPc=pc;
        char *text=lines[pc];
        int indent=indents[pc];
        if(!stmtKind[pc]) stmtKind[pc]=classify(pc);
        int rc=RC_OK;

        switch(stmtKind[pc]){
        case S_BE:
            execBe(text);
            pc++;
            break;
        case S_LOCK:
            execLock(text);
            pc++;
            break;
        case S_SKIP:
            // definisi type dan func: sudah dibaca saat prepass, isinya jalan saat dipanggil
            pc=blockEnd(pc);
            break;
        case S_ERRLANG:
            // sudah dibaca di detectErrLang sebelum program jalan
            if(lineFile[pc]!=0 || indent!=0 || depth>0) nrxFail("errlang hanya boleh di file utama, paling kiri");
            pc++;
            break;
        case S_DELETE:
            execDelete(text);
            pc++;
            break;
        case S_EX: {
            // ex func / ex type / ex be / ex lock: boleh dipakai file lain yang memakai im ke file ini
            char *rest=skipWs(text+2);
            if(indent!=0 || depth>0) nrxFail("ex hanya boleh di paling kiri file, di luar fungsi");
            if(startsWord(rest,"func") || startsWord(rest,"type")){
                pc=blockEnd(pc);
            } else if(startsWord(rest,"be")){
                execBe(rest);
                char vname[64];
                readIdent(skipWs(rest+2),vname,sizeof vname);
                Var *v=findGlobal(vname,NULL);
                if(v && v->owner==curFile()) v->exported=1;
                pc++;
            } else if(startsWord(rest,"lock")){
                execLock(rest)->exported=1;
                pc++;
            } else {
                nrxFail("ex hanya untuk func, type, be, atau lock, contoh: ex func nama()");
            }
            break;
        }
        case S_PRINT: {
            char *s=evalSeqStr(text+5);
            printf("%s\n",s);
            free(s);
            pc++;
            break;
        }
        case S_ASK: {
            // ask tanpa variabel: tunggu input lalu dibuang (mis. "tekan enter")
            Value v=askValue(text,startsWord(text,"ask.int"));
            valFree(&v);
            pc++;
            break;
        }
        case S_WAIT: {
            char *p=text+4;
            // wait(1), wait(0.5), wait.float(0.5), wait.double(0.25)
            if(strncmp(p,".float",6)==0) p+=6;
            else if(strncmp(p,".double",7)==0) p+=7;
            Value v=evalExpr(p);
            double sec=valToNum(v);
            valFree(&v);
            if(sec<0) sec=0;
            if(windowLib) nrxWindowWait(sec);
            else sleepSeconds(sec);
            pc++;
            break;
        }
        case S_FFI:
            // sudah dibaca saat prepass
            pc=ffiEnd(pc)+1;
            break;
        case S_LIB:
            pc++;
            break;
        case S_RETURN:
            if(depth==0) nrxFail("return hanya boleh di dalam fungsi");
            valFree(&retVal);
            retVal=evalSeq(text+6);
            return RC_RETURN;
        case S_BREAK:
            return RC_BREAK;
        case S_CONTINUE:
            return RC_CONTINUE;
        case S_THROW: {
            // throw "pesan": buat error sendiri, bisa ditangkap catch
            char *msg=evalSeqStr(text+5);
            nrxFail("%s",msg);
            break;
        }
        case S_TRY: {
            int next;
            rc=runTry(pc,&next);
            pc=next;
            break;
        }
        case S_CATCH:
            nrxFail("catch harus berada tepat setelah blok try");
            break;
        case S_OPTIONS: {
            int next;
            rc=runOptions(pc,&next);
            pc=next;
            break;
        }
        case S_OPTION:
            nrxFail("option harus berada di dalam blok options");
            break;
        case S_LOOP: {
            int end=blockEnd(pc);
            while(curPc=pc, evalCond(text+4)){
                rc=execRange(pc+1,end);
                if(rc==RC_BREAK){ rc=RC_OK; break; }
                if(rc==RC_RETURN) return rc;
                rc=RC_OK;
            }
            pc=end;
            break;
        }
        case S_FOR: {
            int end=blockEnd(pc);
            char var[64];
            List *items=forItems(text,var,sizeof var);
            for(int i=0;i<items->count;i++){
                curPc=pc;
                setLoopVar(var,valCopy(items->items[i]));
                rc=execRange(pc+1,end);
                if(rc==RC_BREAK){ rc=RC_OK; break; }
                if(rc==RC_RETURN) break;
                rc=RC_OK;
            }
            listRelease(items);
            if(rc==RC_RETURN) return rc;
            pc=end;
            break;
        }
        case S_IF: {
            // telusuri rantai if / else if / else sampai ada cabang yang dijalankan
            int cur=pc, chosen=-1;
            while(cur>=0){
                if(cur!=pc && isElse(lines[cur])){
                    chosen=cur;
                    break;
                }
                curPc=cur;
                if(evalCond(cur==pc ? lines[cur]+2 : lines[cur]+7)){
                    chosen=cur;
                    break;
                }
                cur=nextBranch(cur,stop);
            }
            if(chosen>=0) rc=execRange(chosen+1,blockEnd(chosen));
            // lompat ke akhir seluruh rantai
            int last=pc;
            for(int n; (n=nextBranch(last,stop))>=0; ) last=n;
            pc=blockEnd(last);
            break;
        }
        case S_ELSE:
            // else tanpa if di atasnya
            nrxFail("perintah tidak dikenal: %s",text);
            break;
        case S_WINDOW:
            if(!windowLib) nrxFail("perintah ini butuh library window, tambahkan di atas: lib nrx_window.h");
            pc=nrxWindowExec(pc);
            break;
        case S_CALL: {
            Value v=evalExpr(text);
            valFree(&v);
            pc++;
            break;
        }
        default:
            nrxFail("perintah tidak dikenal: %s",text);
        }

        // return, break, dan continue dari blok anak diteruskan ke loop atau fungsi di atasnya
        if(rc!=RC_OK) return rc;
    }
    return RC_OK;
}

// kumpulkan lib, func, dan isi library sebelum program jalan, jadi urutan penulisannya bebas
static unsigned char inType[MAX_LINES];   // baris di dalam blok type

static Func *newMethod(int i, const char *name, int owner){
    if(funcCount>=128) nrxFailAt(i,"cara menulis fungsi: func nama() atau func nama(a, b)");
    Func *f=&funcs[funcCount++];
    snprintf(f->name,sizeof f->name,"%s",name);
    f->pc=i;
    f->file=lineFile[i];
    f->exported=structs[owner].exported;
    f->ownerType=owner;
    return f;
}

// baca semua blok type: field, init, dan method
static void parseTypes(void){
    for(int i=0;i<lineCount;i++){
        char *ln=lines[i];
        int ex=startsWord(ln,"ex");
        if(ex) ln=skipWs(ln+2);
        if(!startsWord(ln,"type")) continue;
        if(indents[i]!=0) nrxFailAt(i,"type hanya boleh di paling kiri file, di luar fungsi");
        char name[64];
        readIdent(skipWs(ln+4),name,sizeof name);
        if(!isIdent(name) || *skipWs(skipWs(ln+4)+strlen(name)) || structCount>=64){
            nrxFailAt(i,"cara menulis type:\n    type Player\n        nama string\n        hp int");
        }
        if(!strcmp(name,"im") || !strcmp(name,"type") || !strcmp(name,"own")) nrxFailAt(i,"%s adalah kata khusus, tidak boleh jadi nama type",name);
        for(int k=0;k<structCount;k++){
            if(strcmp(structs[k].name,name)==0 && nameClash(structs[k].file,structs[k].exported,lineFile[i],ex)){
                nrxFailAt(i,"type %s sudah ada",name);
            }
        }
        int idx=structCount++;
        StructDef *sd=&structs[idx];
        memset(sd,0,sizeof *sd);
        snprintf(sd->name,sizeof sd->name,"%s",name);
        sd->file=lineFile[i];
        sd->exported=ex;
        sd->pc=i;
        sd->initPc=-1;
        int end=skipBlock(i);
        if(end==i+1) nrxFailAt(i,"type %s masih kosong, tulis field-nya di bawahnya, contoh: hp int",name);
        int mind=indents[i+1];
        for(int j=i;j<end;j++) inType[j]=1;
        for(int j=i+1;j<end;j++){
            if(indents[j]>mind) continue;   // isi init / method
            if(indents[j]<mind) nrxFailAt(j,"isi type %s harus sejajar (indent sama)",name);
            char *m=lines[j];
            if(startsWord(m,"init")){
                if(sd->initPc>=0) nrxFailAt(j,"type %s hanya boleh punya 1 init",name);
                char sig[300];
                snprintf(sig,sizeof sig,"(%s)",skipWs(m+4));
                Func *f=newMethod(j,"init",idx);
                parseFuncSig(j,sig,f);
                sd->initPc=j;
            } else if(startsWord(m,"func")){
                char fname[64];
                funcName(m+4,fname,sizeof fname);
                if(!isIdent(fname)) nrxFailAt(j,"cara menulis fungsi: func nama() atau func nama(a, b)");
                for(int k=0;k<funcCount;k++){
                    if(funcs[k].ownerType==idx && !strcmp(funcs[k].name,fname)) nrxFailAt(j,"method %s sudah ada di type %s",fname,name);
                }
                Func *f=newMethod(j,fname,idx);
                parseFuncSig(j,m,f);
            } else {
                // field: nama tipe  atau  nama tipe = nilai
                if(sd->nfields>=16) nrxFailAt(j,"type maksimal punya 16 field");
                int k=sd->nfields;
                char buf[512];
                snprintf(buf,sizeof buf,"%s",m);
                char *eq=strchr(buf,'=');
                if(eq){
                    snprintf(sd->fdef[k],sizeof sd->fdef[k],"%s",eq+1);
                    trim(sd->fdef[k]);
                    *eq=0;
                    if(!sd->fdef[k][0]) nrxFailAt(j,"nilai bawaan field masih kosong");
                }
                trim(buf);
                char *sp=strpbrk(buf," \t");
                if(!sp) nrxFailAt(j,"field butuh tipe, contoh: hp int  atau  level int = 1");
                *sp=0;
                if(!isIdent(buf)) nrxFailAt(j,"nama field tidak valid: %s",buf);
                for(int q=0;q<k;q++) if(!strcmp(sd->fields[q],buf)) nrxFailAt(j,"field %s sudah ada",buf);
                snprintf(sd->fields[k],sizeof sd->fields[k],"%s",buf);
                snprintf(sd->ftype[k],sizeof sd->ftype[k],"%s",sp+1);
                trim(sd->ftype[k]);
                sd->nfields++;
            }
        }
    }
}

static void prepass(void){
    for(int i=0;i<lineCount;i++){
        if(!startsWord(lines[i],"lib")) continue;
        char name[256];
        snprintf(name,sizeof name,"%s",lines[i]+3);
        trim(name);
        if(strcmp(name,"nrx_window.h")==0) windowLib=1;
        else if(strcmp(name,"nrx_rand.h")==0) randLib=1;
        else if(strcmp(name,"nrx_crudfile.h")==0) fileLib=1;
        else if(strcmp(name,"nrx_keyboard.h")==0) keyboardLib=1;
        else if(strcmp(name,"nrx_console.h")==0) consoleLib=1;
        else if(strcmp(name,"nrx_sound.h")==0) soundLib=1;
        else if(strcmp(name,"nrx_json.h")==0) jsonLib=1;
        else if(strcmp(name,"nrx_3d.h")==0) threeDLib=1;
        else if(strcmp(name,"nrx_http.h")==0) httpLib=1;
        else nrxFailAt(i,"library %s tidak ditemukan (yang ada: nrx_window.h, nrx_rand.h, nrx_crudfile.h, nrx_keyboard.h, nrx_console.h, nrx_sound.h, nrx_json.h, nrx_http.h, nrx_3d.h)",name);
    }
    for(int i=0;i<lineCount;i++){
        if(startsWord(lines[i],"ex") && indents[i]!=0) nrxFailAt(i,"ex hanya boleh di paling kiri file, di luar fungsi");
    }
    markFfiBlocks();
    parseTypes();
    for(int i=0;i<lineCount;i++){
        // ex func nama(): fungsi yang boleh dipakai file lain yang memakai im ke file ini
        if(inFfi[i] || inType[i]) continue;
        char *ln=lines[i];
        int ex=startsWord(ln,"ex");
        if(ex) ln=skipWs(ln+2);
        if(!startsWord(ln,"func")) continue;
        if(ex && indents[i]!=0) nrxFailAt(i,"ex hanya boleh di paling kiri file (tidak menjorok)");
        char name[64];
        funcName(ln+4,name,sizeof name);
        if(!isIdent(name) || funcCount>=128) nrxFailAt(i,"cara menulis fungsi: func nama() atau func nama(a, b)");
        if(!strcmp(name,"im")) nrxFailAt(i,"im adalah kata khusus untuk memasukkan file, tidak boleh jadi nama fungsi");
        if(!strcmp(name,"type")) nrxFailAt(i,"type adalah kata khusus untuk membuat tipe baru, tidak boleh jadi nama fungsi");
        for(int k=0;k<funcCount;k++){
            if(funcs[k].ownerType<0 && strcmp(funcs[k].name,name)==0 && nameClash(funcs[k].file,funcs[k].exported,lineFile[i],ex)){
                nrxFailAt(i,"fungsi %s sudah ada (di %s:%d)",name,files[lineFile[funcs[k].pc]],lineNo[funcs[k].pc]);
            }
        }
        Func *f=&funcs[funcCount];
        snprintf(f->name,sizeof f->name,"%s",name);
        f->pc=i;
        f->file=lineFile[i];
        f->exported=ex;
        f->ownerType=-1;
        parseFuncSig(i,ln,f);
        funcCount++;
    }
    for(int i=0;i<lineCount;i++){
        char *ln=lines[i];
        if(startsWord(ln,"ex")) ln=skipWs(ln+2);
        if(startsWord(ln,"struct")) nrxFailAt(i,"struct sudah diganti type, contoh:\n    type Player\n        nama string\n        hp int");
    }
    // nama type tidak boleh sama dengan fungsi
    for(int s=0;s<structCount;s++){
        for(int k=0;k<funcCount;k++){
            if(funcs[k].ownerType<0 && strcmp(funcs[k].name,structs[s].name)==0 && nameClash(funcs[k].file,funcs[k].exported,structs[s].file,structs[s].exported)){
                nrxFailAt(structs[s].pc,"nama %s sudah dipakai fungsi",structs[s].name);
            }
        }
    }
    // tipe field, parameter, dan hasil fungsi: sekarang semua nama type sudah dikenal
    for(int s=0;s<structCount;s++){
        StructDef *sd=&structs[s];
        for(int j=0;j<sd->nfields;j++){
            curPc=sd->pc;
            resolvedElemName=NULL;
            if(!sd->ftype[j][0] || !resolveType(sd->ftype[j],&sd->fkind[j],&sd->felem[j],&sd->fsname[j])){
                nrxFailAt(sd->pc,"tipe field %s tidak dikenal: %s",sd->fields[j],sd->ftype[j]);
            }
            sd->fename[j]=resolvedElemName;
        }
    }
    for(int k=0;k<funcCount;k++){
        Func *f=&funcs[k];
        curPc=f->pc;
        for(int j=0;j<f->nparams;j++){
            if(!resolveType(f->ptype[j],&f->pkind[j],&f->pelem[j],&f->psname[j])){
                nrxFailAt(f->pc,"tipe parameter %s tidak dikenal, pakai: int, float, double, bool, string, list, map, json, atau nama type",f->ptype[j]);
            }
        }
        if(!resolveType(f->rtype,&f->rkind,&f->relem,&f->rsname)){
            nrxFailAt(f->pc,"tipe hasil %s tidak dikenal, pakai: int, float, double, bool, string, list, map, json, atau nama type",f->rtype);
        }
    }
    curPc=-1;
    parseFfiBlocks();
    nrxWindowPrepass(windowLib);
}

// ---------------- membaca file + im ----------------

static char *fullPaths[64];


// catat im dari file importer ke file src; im "src" (list kosong) membuka semua yang di-ex
static void addImport(int importer, int src, const char *list, int line, const char *written){
    char *old=imNames[importer][src];
    if(importer==src) return;
    if(!imText[importer][src]) imText[importer][src]=strdup(written);
    if(!old){
        imNames[importer][src]=strdup(list);
        imLine[importer][src]=line;
    } else if(old[0] && !list[0]){
        free(old);
        imNames[importer][src]=strdup("");
    } else if(old[0]){
        char *u=malloc(strlen(old)+strlen(list)+1);
        strcpy(u,old);
        strcat(u,list+1);
        free(old);
        imNames[importer][src]=u;
    }
}

// baca file .nrxl ke lines[]; baris "im ..." diganti seluruh isi file yang dimasukkan
static int loadFile(const char *path, int fromFile, int fromLine){
    char full[1024];
    #ifdef _WIN32
    if(!_fullpath(full,path,sizeof full)) snprintf(full,sizeof full,"%s",path);
    #else
    if(!realpath(path,full)) snprintf(full,sizeof full,"%s",path);
    #endif
    // file yang sama cukup dimuat sekali (juga mencegah im yang saling memanggil)
    for(int i=0;i<fileCount;i++){
        if(strcmp(fullPaths[i],full)==0) return i;
    }
    SrcReader f;
    const char *from=fromFile<0 ? NULL : files[fromFile];
    if(!srcOpen(&f,path)) loadFail(from,fromLine,"file tidak ditemukan: %s",path);
    if(fileCount>=64) loadFail(from,fromLine,"terlalu banyak file di-im, maksimal 64: %s",path);
    int fi=fileCount++;
    files[fi]=strdup(path);
    fullPaths[fi]=strdup(full);

    char line[512];
    int srcLine=0;
    while(srcGets(line,sizeof line,&f)){
        srcLine++;
        line[strcspn(line,"\r\n")]=0;
        // buang komentar # yang tidak berada di dalam tanda kutip
        int inq=0;
        for(char *c=line; *c; c++){
            if(inq && *c=='\\' && c[1]){
                c++;
                continue;
            }
            if(*c=='"') inq=!inq;
            else if(*c=='#' && !inq){
                *c=0;
                break;
            }
        }
        int len=strlen(line);
        while(len>0 && (line[len-1]==' '||line[len-1]=='\t')) line[--len]=0;
        if(len==0) continue;
        int sp=strspn(line," \t");

        if(startsWord(line+sp,"im")){
            // im "utils.nrxl" atau im utils; lokasinya relatif terhadap file ini
            const char *after=line+sp+2;
            while(*after==' ' || *after=='\t') after++;
            if(*after=='(' || !*after) loadFail(path,srcLine,"im hanya untuk memasukkan file .nrxl, contoh: im hitung (bukan fungsi im())");
            if(sp!=0) loadFail(path,srcLine,"im harus ditulis paling kiri (tidak menjorok)");
            // im luas, kali2 from "hitung": hanya nama itu yang boleh dipakai dari hitung.nrxl
            char list[512]="";
            char name[256];
            snprintf(name,sizeof name,"%s",after);
            char *fromw=strstr(name," from ");
            if(fromw && !memchr(name,'"',fromw-name)){
                *fromw=0;
                char *n=name;
                strcpy(list,",");
                for(;;){
                    while(*n==' ' || *n=='	') n++;
                    char id[64];
                    int k=0;
                    while(idChar(*n) && k<63) id[k++]=*n++;
                    id[k]=0;
                    while(*n==' ' || *n=='	') n++;
                    if(!isIdent(id) || (*n && *n!=',') || strlen(list)+k+2>=sizeof list) loadFail(path,srcLine,"cara menulis: im nama, nama2 from \"file\"");
                    strcat(list,id);
                    strcat(list,",");
                    if(!*n) break;
                    n++;
                }
                memmove(name,fromw+6,strlen(fromw+6)+1);
            }
            trim(name);
            if(!name[0]) loadFail(path,srcLine,"cara menulis: im nama, nama2 from \"file\"");
            int nl=strlen(name);
            if(nl>=2 && name[0]=='"' && name[nl-1]=='"'){
                memmove(name,name+1,nl-2);
                name[nl-2]=0;
            }
            char written[256];
            snprintf(written,sizeof written,"%s",name);
            // tanpa ekstensi (tidak ada titik setelah / terakhir): tambahkan .nrxl
            const char *base=name;
            for(const char *c=name; *c; c++) if(*c=='/' || *c=='\\') base=c+1;
            if(!strchr(base,'.')) strncat(name,".nrxl",sizeof name-strlen(name)-1);
            char child[1024];
            const char *slash=strrchr(path,'/');
            const char *bslash=strrchr(path,'\\');
            if(bslash>slash) slash=bslash;
            int absolute=name[0]=='/' || name[0]=='\\' || (name[0] && name[1]==':');
            if(slash && !absolute) snprintf(child,sizeof child,"%.*s/%s",(int)(slash-path),path,name);
            else snprintf(child,sizeof child,"%s",name);
            int ci=loadFile(child,fi,srcLine);
            addImport(fi,ci,list,srcLine,written);
            continue;
        }

        if(lineCount>=MAX_LINES) loadFail(path,srcLine,"program terlalu panjang (maksimal %d baris)",MAX_LINES);
        strcpy(lines[lineCount],line+sp);
        indents[lineCount]=sp;
        lineNo[lineCount]=srcLine;
        lineFile[lineCount]=fi;
        lineCount++;
    }
    srcClose(&f);
    return fi;
}

// setiap nama di im ... from "file" harus di-ex di file itu
static void checkImNames(void){
    for(int a=0;a<fileCount;a++){
        for(int b=0;b<fileCount;b++){
            const char *list=imNames[a][b];
            if(!list || !list[0]) continue;
            const char *p=list+1;
            while(*p){
                char name[64];
                int k=0;
                while(*p && *p!=',' && k<63) name[k++]=*p++;
                name[k]=0;
                if(*p==',') p++;
                int found=0;
                for(int i=0;i<lineCount && !found;i++){
                    if(lineFile[i]!=b || !startsWord(lines[i],"ex")) continue;
                    char *r=skipWs(lines[i]+2);
                    const char *kw[]={"func","type","be","lock"};
                    for(int j=0;j<4 && !found;j++){
                        if(!startsWord(r,kw[j])) continue;
                        char id[64];
                        readIdent(skipWs(r+strlen(kw[j])),id,sizeof id);
                        found=!strcmp(id,name);
                    }
                }
                if(!found) loadFail(files[a],imLine[a][b],"%s tidak di-export di %s (tulis di sana: ex func %s, ex be %s = ..., atau ex struct %s)",name,files[b],name,name,name);
            }
        }
    }
}

// ---------------- cek tipe sebelum program jalan ----------------
// pohon AST ditelusuri sekali sebelum baris pertama dijalankan. Yang dicek hanya yang
// pasti error saat jalan, dengan pesan yang sama: isi variabel yang jenisnya sudah
// diketahui, argumen fungsi bertipe, jumlah argumen, nilai return, field type, dan lock.
// Kalau jenis sebuah nilai belum bisa dipastikan, baris itu dibiarkan dicek saat jalan.
// Isi blok try tidak dicek di sini, karena try memang dipakai untuk menangkap error.

typedef struct { int k, e; const char *sn; } ST;   // sn: nama type (atau nama type isi list)
static const ST ST_ANY={K_ANY,K_ANY,NULL};

typedef struct { char name[64]; ST t; int locked; } CheckVar;
typedef struct { CheckVar v[256]; int n; } CheckTab;

static CheckVar *tabGet(CheckTab *tb, const char *name){
    for(int i=0;i<tb->n;i++) if(!strcmp(tb->v[i].name,name)) return &tb->v[i];
    return NULL;
}

static void tabPut(CheckTab *tb, const char *name, ST t, int locked){
    if(tabGet(tb,name) || tb->n>=256) return;
    CheckVar *c=&tb->v[tb->n++];
    snprintf(c->name,sizeof c->name,"%s",name);
    c->t=t;
    c->locked=locked;
}

static void tabDel(CheckTab *tb, const char *name){
    CheckVar *c=tabGet(tb,name);
    if(c) *c=tb->v[--tb->n];
}

static ST st(int k, int e, const char *sn){
    ST t={k,e,sn};
    return t;
}

static int stKnown(ST t){
    return t.k!=K_ANY && t.k!=K_MIXED;
}

// nama jenis untuk pesan error, sama dengan valKindName saat jalan
static const char *stName(ST t){
    static char buf[2][48];
    static int turn=0;
    if(t.k==K_STRUCT && t.sn) return t.sn;
    if((t.k==K_LIST || t.k==K_MAP) && t.e!=K_ANY && t.e!=K_MIXED){
        turn=!turn;
        snprintf(buf[turn],sizeof buf[turn],"%s %s",kindName(t.k),t.e==K_STRUCT && t.sn ? t.sn : kindName(t.e));
        return buf[turn];
    }
    return kindName(t.k);
}

// sama dengan fitKind, tapi untuk jenis yang diketahui sebelum jalan; 0 juga berarti "belum pasti"
static int stFit(int k, const char *sn, int e, ST g){
    if(k==K_ANY || k==K_MIXED || !stKnown(g)) return 0;
    if(k==K_INT) return g.k==K_INT ? 0 : isNumKind(g.k) ? 1 : 2;
    if(k==K_DOUBLE || k==K_FLOAT) return isNumKind(g.k) ? 0 : 2;
    if(k==K_STRUCT) return g.k==K_STRUCT && (!sn || !g.sn || !strcmp(sn,g.sn)) ? 0 : 2;
    if(k!=g.k) return 2;
    if(e==K_ANY || e==K_MIXED || g.e==K_ANY || g.e==K_MIXED) return 0;
    if(e==g.e) return e==K_STRUCT && g.sn && sn && strcmp(g.sn,sn) ? 2 : 0;
    if((e==K_DOUBLE || e==K_FLOAT) && g.e==K_INT) return 0;
    return 2;
}

static int isBuiltinName(const char *name){
    static const char *names[]={"abs","add","ceil","contains","date","exit","float","floor","im",
        "indexOf","int","join","keys","len","lower","max","min","pow","range","remove","replace",
        "reverse","round","slice","sort","split","sqrt","sin","cos","tan","atan2","pi","str","substr","takeLast","time","timer",
        "trim","type","typeOf","upper","values","write","newBuffer","freeBuffer","peek","poke",
        "peekText","callback","filled","array"};
    for(size_t i=0;i<sizeof names/sizeof *names;i++) if(!strcmp(name,names[i])) return 1;
    return findFfi(name) || nrxKeyboardOwns(name) || nrxWindowFuncOwns(name) || nrxSoundOwns(name)
        || nrxJsonOwns(name) || nrxHttpOwns(name) || nrx3dOwns(name) || nrxConsoleOwns(name) || nrxRandOwns(name) || nrxFileOwns(name);
}

// fungsi buatan sendiri yang akan dipanggil untuk nama ini (urutannya sama dengan callNamed)
static Func *userFuncFor(const char *name){
    if(isBuiltinName(name) || findStruct(name,NULL)) return NULL;
    int fpc=findFuncVisible(name,NULL);
    return fpc>=0 ? funcAt(fpc) : NULL;
}

static ST fieldST(const char *sname, const char *field){
    if(!sname) return ST_ANY;
    int si=structIndex(sname);
    if(si<0) return ST_ANY;
    StructDef *sd=&structs[si];
    for(int i=0;i<sd->nfields;i++){
        if(strcmp(sd->fields[i],field)) continue;
        return st(sd->fkind[i],sd->felem[i],sd->fkind[i]==K_STRUCT ? sd->fsname[i] : sd->fename[i]);
    }
    return ST_ANY;
}

static ST funcResult(Func *f){
    return st(f->rkind,f->relem,f->rkind==K_STRUCT ? f->rsname : NULL);
}

// jenis hasil + - * / % kalau dua sisinya diketahui; selain itu belum pasti
static ST binST(ST a, int op, ST b){
    if(isNumKind(a.k) && isNumKind(b.k)){
        if(op=='/') return st(K_DOUBLE,K_ANY,NULL);
        return st(a.k==K_INT && b.k==K_INT ? K_INT : K_DOUBLE,K_ANY,NULL);
    }
    if(op!='+' || !stKnown(a) || !stKnown(b)) return ST_ANY;
    // + selain angka: menggabung teks (list, map, dan type ditolak saat jalan)
    int ca=a.k==K_LIST || a.k==K_MAP || a.k==K_STRUCT, cb=b.k==K_LIST || b.k==K_MAP || b.k==K_STRUCT;
    return ca || cb ? ST_ANY : st(K_STRING,K_ANY,NULL);
}

// jenis isi yang sama untuk semua item list/map literal
static void mergeElem(ST *acc, int *first, ST t){
    if(*first){
        *acc=t;
        *first=0;
        return;
    }
    if(!stKnown(*acc)) return;
    if(isNumKind(acc->k) && isNumKind(t.k)){
        if(t.k!=acc->k) acc->k=K_DOUBLE;
        return;
    }
    if(t.k!=acc->k || (t.k==K_STRUCT && (!t.sn || !acc->sn || strcmp(t.sn,acc->sn)))) *acc=ST_ANY;
}

static ST inferNode(Node *x, CheckTab *tb){
    switch(x->kind){
    case N_LIT:
        return st(valKind(x->lit),K_ANY,NULL);
    case N_VAR: {
        CheckVar *c=tabGet(tb,x->name);
        return c ? c->t : ST_ANY;
    }
    case N_LIST: case N_MAP: {
        ST acc=ST_ANY;
        int first=1;
        int step=x->kind==N_MAP ? 2 : 1;
        for(int i=x->kind==N_MAP ? 1 : 0;i<x->n;i+=step) mergeElem(&acc,&first,inferNode(x->kids[i],tb));
        int k=x->kind==N_LIST ? K_LIST : K_MAP;
        if(!stKnown(acc) || acc.k==K_LIST || acc.k==K_MAP) return st(k,K_ANY,NULL);
        return st(k,acc.k,acc.k==K_STRUCT ? acc.sn : NULL);
    }
    case N_CALL: {
        StructDef *sd=findStruct(x->name,NULL);
        if(sd && !isBuiltinName(x->name)) return st(K_STRUCT,K_ANY,sd->name);
        if(!strcmp(x->name,"len") || !strcmp(x->name,"indexOf") || !strcmp(x->name,"newBuffer")
            || !strcmp(x->name,"callback")) return st(K_INT,K_ANY,NULL);
        if(!strcmp(x->name,"peekText") || !strcmp(x->name,"httpGet") || !strcmp(x->name,"httpPost")
            || !strcmp(x->name,"httpStart") || !strcmp(x->name,"httpResult") || !strcmp(x->name,"httpPartial"))
            return st(K_STRING,K_ANY,NULL);
        if(!strcmp(x->name,"httpStatus") || !strcmp(x->name,"commandStatus")
            || !strcmp(x->name,"replaceInFile")) return st(K_INT,K_ANY,NULL);
        if(!strcmp(x->name,"runCommand") || !strcmp(x->name,"typed3d") || !strcmp(x->name,"clipboard3d"))
            return st(K_STRING,K_ANY,NULL);
        if(!strcmp(x->name,"textWidth2d")) return st(K_INT,K_ANY,NULL);
        if(!strcmp(x->name,"click3d") || !strcmp(x->name,"httpDone")) return st(K_BOOL,K_ANY,NULL);
        if(!strcmp(x->name,"wheel3d")) return st(K_DOUBLE,K_ANY,NULL);
        if((!strcmp(x->name,"sin") || !strcmp(x->name,"cos") || !strcmp(x->name,"tan")
            || !strcmp(x->name,"atan2") || !strcmp(x->name,"pi")) && !findFfi(x->name)) return st(K_DOUBLE,K_ANY,NULL);
        if(!strcmp(x->name,"array")) return st(K_ARRAY,K_ANY,NULL);
        if(!strcmp(x->name,"range")) return st(K_LIST,K_INT,NULL);
        if(!strcmp(x->name,"filled") && x->n==2){
            ST e=inferNode(x->kids[1],tb);
            if(stKnown(e) && e.k!=K_LIST && e.k!=K_MAP) return st(K_LIST,e.k,e.k==K_STRUCT ? e.sn : NULL);
            return st(K_LIST,K_ANY,NULL);
        }
        if(!strcmp(x->name,"str") || !strcmp(x->name,"typeOf") || !strcmp(x->name,"upper")
            || !strcmp(x->name,"lower") || !strcmp(x->name,"trim")) return st(K_STRING,K_ANY,NULL);
        Func *f=userFuncFor(x->name);
        return f ? funcResult(f) : ST_ANY;
    }
    case N_FIELD: {
        ST b=inferNode(x->kids[0],tb);
        return b.k==K_STRUCT ? fieldST(b.sn,x->name) : ST_ANY;
    }
    case N_INDEX: {
        ST b=inferNode(x->kids[0],tb);
        if(b.k==K_STRING) return st(K_STRING,K_ANY,NULL);
        if(b.k==K_ARRAY) return st(K_DOUBLE,K_ANY,NULL);
        if((b.k==K_LIST || b.k==K_MAP) && b.e!=K_ANY && b.e!=K_MIXED && b.e!=K_LIST && b.e!=K_MAP)
            return st(b.e,K_ANY,b.e==K_STRUCT ? b.sn : NULL);
        return ST_ANY;
    }
    case N_METHOD: {
        ST b=inferNode(x->kids[0],tb);
        if(b.k!=K_STRUCT || !b.sn) return ST_ANY;
        int si=structIndex(b.sn);
        int mpc=si>=0 ? findMethod(si,x->name) : -1;
        return mpc>=0 ? funcResult(funcAt(mpc)) : ST_ANY;
    }
    case N_NEG: {
        ST a=inferNode(x->kids[0],tb);
        return isNumKind(a.k) ? st(a.k,K_ANY,NULL) : ST_ANY;
    }
    case N_BIN:
        return binST(inferNode(x->kids[0],tb),x->op,inferNode(x->kids[1],tb));
    case N_CMP: case N_IN: case N_NOT: case N_AND: case N_OR:
        return st(K_BOOL,K_ANY,NULL);
    case N_CONCAT:
        return st(K_STRING,K_ANY,NULL);
    }
    return ST_ANY;
}

static void checkRange(int start, int stop, int baseIndent, Func *f, CheckTab *tb);

// fungsi dengan parameter tanpa tipe dicek lagi untuk tiap kombinasi jenis argumen
// yang dipakai, seperti compiler nanti membuat satu versi per kombinasi:
// func dobel(x) dipanggil dobel(2) dan dobel("a") dicek sebagai x int dan x string
static void checkVersion(Func *f, ST *args, int n, const char *ownType){
    static char done[512][160];
    static int ndone=0, nesting=0;
    char key[160];
    int k=snprintf(key,sizeof key,"%d",f->pc), any=0;
    for(int i=0;i<n && k<(int)sizeof key-40;i++){
        if(f->pkind[i]==K_ANY && stKnown(args[i])) any=1;
        k+=snprintf(key+k,sizeof key-k,":%d/%d/%s",args[i].k,args[i].e,args[i].sn ? args[i].sn : "");
    }
    if(!any || nesting>=6) return;
    for(int i=0;i<ndone;i++) if(!strcmp(done[i],key)) return;
    if(ndone<512) snprintf(done[ndone++],sizeof done[0],"%s",key);
    int end=blockEnd(f->pc);
    if(end<=f->pc+1) return;
    CheckTab *tb=calloc(1,sizeof(CheckTab));
    if(ownType) tabPut(tb,"own",st(K_STRUCT,K_ANY,ownType),0);
    for(int i=0;i<f->nparams;i++){
        if(f->pkind[i]!=K_ANY) tabPut(tb,f->params[i],st(f->pkind[i],f->pelem[i],f->psname[i]),0);
        else if(i<n && stKnown(args[i])) tabPut(tb,f->params[i],args[i],0);
    }
    int savedPc=curPc;
    nesting++;
    checkRange(f->pc+1,end,indents[f->pc+1],f,tb);
    nesting--;
    curPc=savedPc;
    free(tb);
}

// argumen untuk parameter bertipe; pesannya sama dengan bindParam
static void checkArgs(int pc, Func *f, Node *x, int from, CheckTab *tb){
    int n=x->n-from;
    if(n<f->nrequired || n>f->nparams){
        if(f->nrequired==f->nparams) nrxFailAt(pc,"fungsi %s butuh %d argumen, tapi diberi %d",f->name,f->nparams,n);
        nrxFailAt(pc,"fungsi %s butuh %d sampai %d argumen, tapi diberi %d",f->name,f->nrequired,f->nparams,n);
    }
    for(int i=0;i<n;i++){
        ST g=inferNode(x->kids[from+i],tb);
        int r=stFit(f->pkind[i],f->psname[i],f->pelem[i],g);
        if(r==1) nrxFailAt(pc,"parameter %s fungsi %s bertipe int, tidak bisa diisi angka desimal; pakai int(...)",f->params[i],f->name);
        if(r==2) nrxFailAt(pc,"parameter %s fungsi %s bertipe %s, tidak bisa diisi %s",f->params[i],f->name,f->ptype[i],stName(g));
    }
    ST args[8];
    for(int i=0;i<n && i<8;i++) args[i]=inferNode(x->kids[from+i],tb);
    const char *ownType=NULL;
    if(from==1){
        ST o=inferNode(x->kids[0],tb);
        ownType=o.sn;
    }
    checkVersion(f,args,n<8 ? n : 8,ownType);
}

// operator yang pasti error: - * / % dengan bukan angka, + dengan list/map,
// -x untuk bukan angka, dan membandingkan angka dengan bukan angka
static void checkOps(int pc, Node *x, CheckTab *tb){
    if(x->kind==N_BIN){
        ST a=inferNode(x->kids[0],tb), b=inferNode(x->kids[1],tb);
        if(x->op=='+'){
            if(a.k==K_LIST || b.k==K_LIST) nrxFailAt(pc,"operator + tidak bisa untuk list, pakai add(list, nilai)");
            if(a.k==K_MAP || b.k==K_MAP || a.k==K_STRUCT || b.k==K_STRUCT) nrxFailAt(pc,"operator + tidak bisa untuk map, pakai be map[key] = nilai");
            return;
        }
        ST bad=stKnown(a) && !isNumKind(a.k) ? a : b;
        if(!stKnown(bad) || isNumKind(bad.k)) return;
        if(bad.k==K_STRING) nrxFailAt(pc,"operator %c butuh angka, tapi dapat teks; ubah dulu pakai int(...) atau float(...)",x->op);
        nrxFailAt(pc,"operator %c butuh angka, tapi dapat %s",x->op,stName(bad));
    } else if(x->kind==N_NEG){
        ST a=inferNode(x->kids[0],tb);
        if(stKnown(a) && !isNumKind(a.k)) nrxFailAt(pc,"tanda - butuh angka");
    } else if(x->kind==N_CMP){
        ST a=inferNode(x->kids[0],tb), b=inferNode(x->kids[1],tb);
        if(stKnown(a) && stKnown(b) && isNumKind(a.k)!=isNumKind(b.k))
            nrxFailAt(pc,"tidak bisa membandingkan %s dengan %s; ubah dulu pakai int(...), float(...), atau str(...)",stName(a),stName(b));
    }
}

// semua pemanggilan fungsi dan operator di dalam satu ekspresi
static void checkCalls(int pc, Node *x, CheckTab *tb){
    for(int i=0;i<x->n;i++) checkCalls(pc,x->kids[i],tb);
    checkOps(pc,x,tb);
    if(x->kind==N_CALL){
        Func *f=userFuncFor(x->name);
        if(f) checkArgs(pc,f,x,0,tb);
    } else if(x->kind==N_METHOD){
        ST b=inferNode(x->kids[0],tb);
        if(b.k!=K_STRUCT || !b.sn) return;
        int si=structIndex(b.sn);
        int mpc=si>=0 ? findMethod(si,x->name) : -1;
        if(mpc>=0) checkArgs(pc,funcAt(mpc),x,1,tb);
    }
}

static ST checkExpr(int pc, const char *src, char mode, CheckTab *tb){
    curPc=pc;
    Node *x=astFor(src,mode);
    checkCalls(pc,x,tb);
    return inferNode(x,tb);
}

// nilai g disimpan ke variabel bernama name berjenis want; pesannya sama dengan checkVarKind
static void checkStore(int pc, const char *name, ST want, ST g){
    int r=stFit(want.k,want.sn,want.e,g);
    if(r==1) nrxFailAt(pc,"%s bertipe int, tidak bisa diisi angka desimal; pakai int(...) untuk membulatkan ke bawah",name);
    if(r==2){
        if((want.k==K_LIST || want.k==K_MAP) && g.k==want.k)
            nrxFailAt(pc,"%s berisi %s, tidak bisa diisi %s berisi %s",name,kindName(want.e),kindName(want.k),kindName(g.e));
        nrxFailAt(pc,"%s bertipe %s, tidak bisa diisi %s",name,stName(want),stName(g));
    }
}

// diisi checkBe untuk be nama = nilai (bukan field / index): dipakai globalNote
static char beName[64];
static ST beType;

// be ... / lock ...: baris lengkapnya text, top = baris ini ada di tingkat paling luar scope
static void checkBe(int pc, char *text, int isLock, int top, CheckTab *tb){
    char *p=skipWs(text+(isLock ? 4 : 2));
    char name[64], field[64]="";
    if(!idStart(*p)) return;
    p=readIdent(p,name,sizeof name);
    if(*p=='.' && idStart(p[1])){
        p=readIdent(p+1,field,sizeof field);
    }
    int deep=*p=='[' || *p=='.';
    if(deep){
        // be xs[i] = ... / be a.b.c = ...: cukup cek pemanggilan fungsi di nilainya
        char *eq=strchr(p,'=');
        if(eq && eq[1]) checkExpr(pc,eq+1+(eq[1]=='='),'s',tb);
        return;
    }
    p=skipWs(p);
    char type[32]="", elemType[32]="";
    if(!field[0] && idStart(*p)){
        p=readIdent(p,type,sizeof type);
        if(!isType(type)) return;
        p=skipWs(p);
        if((!strcmp(type,"list") || !strcmp(type,"map")) && idStart(*p)){
            p=readIdent(p,elemType,sizeof elemType);
            if(!isType(elemType)) return;
            p=skipWs(p);
        }
    }
    char op=0;
    if(*p=='=' && p[1]!='=') op='=', p++;
    else if(*p && strchr("+-*/%",*p) && p[1]=='=') op=*p, p+=2;
    else if(*p) return;
    p=skipWs(p);
    if(op && !*p) return;

    CheckVar *cur=tabGet(tb,name);
    ST g=ST_ANY;
    if(op && !startsWord(p,"ask") && !startsWord(p,"ask.int")) g=checkExpr(pc,p,'s',tb);
    if(!field[0] && op=='='){
        snprintf(beName,sizeof beName,"%s",name);
        beType=type[0] ? st(kindFromName(type),elemType[0] ? kindFromName(elemType) : K_ANY,NULL) : g;
    }

    if(field[0]){
        // be p.hp = nilai: jenis field sudah ditulis di type
        if(!cur || cur->t.k!=K_STRUCT || !cur->t.sn) return;
        ST want=fieldST(cur->t.sn,field);
        if(!stKnown(want)) return;
        if(op!='=') g=binST(want,op,g);
        int r=stFit(want.k,want.sn,want.e,g);
        if(r==1) nrxFailAt(pc,"field %s di type %s bertipe int, tidak bisa diisi angka desimal; pakai int(...)",field,cur->t.sn);
        if(r==2) nrxFailAt(pc,"field %s di type %s bertipe %s, tidak bisa diisi %s",field,cur->t.sn,stName(want),stName(g));
        return;
    }
    if(cur && cur->locked && !isLock) nrxFailAt(pc,"%s di-lock, nilainya tidak bisa diubah",name);
    ST declared=ST_ANY;
    if(type[0]){
        declared=st(kindFromName(type),elemType[0] ? kindFromName(elemType) : K_ANY,NULL);
        if(cur && stKnown(cur->t) && (cur->t.k!=declared.k
            || (declared.e!=K_ANY && cur->t.e!=K_ANY && cur->t.e!=declared.e))){
            nrxFailAt(pc,"%s sudah bertipe %s, tidak bisa diganti jadi %s",name,cur->t.k==K_STRUCT ? cur->t.sn : kindName(cur->t.k),type);
        }
    }
    ST want=cur ? cur->t : declared;
    if(stKnown(want) && op){
        ST val=op=='=' ? g : binST(want,op,g);
        checkStore(pc,name,want,val);
    }
    if(!cur && top && (op=='=' || !op)){
        ST t=type[0] ? declared : g;
        // [] atau {} tanpa jenis isi: jenisnya baru diketahui saat jalan
        if(stKnown(t)) tabPut(tb,name,t,isLock);
    }
}

// delete a, b: variabel itu boleh dibuat lagi dengan jenis lain
static void checkDelete(char *text, CheckTab *tb){
    char *p=skipWs(text+6);
    while(idStart(*p)){
        char name[64];
        p=skipWs(readIdent(p,name,sizeof name));
        tabDel(tb,name);
        if(*p==',') p=skipWs(p+1);
    }
}

// Variabel global: semua be dan for di luar fungsi, di kedalaman mana pun (mis. di dalam
// loop utama), sudah jadi global. Di dalam fungsi, be ke nama yang sama mengubah global
// itu, bukan membuat variabel lokal; kalau jenisnya beda, program baru mati saat jalan.
// Tabel ini menangkapnya sebelum jalan dan menyebut baris asal variabelnya.
typedef struct { char name[64]; ST t; int pc, file; } GlobVar;
static GlobVar globVars[2048];
static int globVarCount=0;

static GlobVar *globFind(const char *name, int file){
    for(int i=0;i<globVarCount;i++){
        if(globVars[i].file==file && !strcmp(globVars[i].name,name)) return &globVars[i];
    }
    return NULL;
}

static int isParamOf(Func *f, const char *name){
    if(f->ownerType>=0 && !strcmp(name,"own")) return 1;
    for(int j=0;j<f->nparams;j++) if(!strcmp(f->params[j],name)) return 1;
    return 0;
}

// jenis isi satu putaran for ... in
static ST forElemST(ST it){
    if(it.k==K_STRING || it.k==K_MAP) return st(K_STRING,K_ANY,NULL);
    if(it.k==K_ARRAY) return st(K_DOUBLE,K_ANY,NULL);
    if(it.k==K_LIST && it.e!=K_ANY && it.e!=K_MIXED) return st(it.e,K_ANY,it.e==K_STRUCT ? it.sn : NULL);
    return ST_ANY;
}

// nama variabel (be atau for) bernilai t di baris pc; f = fungsi tempatnya, NULL di luar fungsi
static void globalNote(int pc, Func *f, const char *name, ST t, int isFor){
    int file=lineFile[pc];
    GlobVar *g=globFind(name,file);
    if(!f){
        if(!g){
            // jenis belum pasti juga dicatat: loop di fungsi tetap tidak boleh menimpanya
            if(globVarCount<2048){
                g=&globVars[globVarCount++];
                snprintf(g->name,sizeof g->name,"%s",name);
                g->t=t;
                g->pc=pc;
                g->file=file;
            }
            return;
        }
        if(!stKnown(g->t)){
            g->t=t;
            g->pc=pc;
        }
        else if(stFit(g->t.k,g->t.sn,g->t.e,t))
            nrxFailAt(pc,"%s sudah dipakai sebagai %s di baris %d, tidak bisa diisi %s",name,stName(g->t),lineNo[g->pc],stName(t));
        return;
    }
    if(!g || isParamOf(f,name)) return;
    // variabel for di fungsi selalu lokal (setLoopVar), jadi tidak menyentuh global
    if(isFor) return;
    if(stKnown(g->t) && stKnown(t) && stFit(g->t.k,g->t.sn,g->t.e,t))
        nrxFailAt(pc,"%s sudah jadi variabel global bertipe %s (baris %d); di fungsi %s, be %s mengubah variabel itu, bukan membuat yang baru; pakai nama lain",name,stName(g->t),lineNo[g->pc],f->name,name);
}

static void globalDelete(char *text, int file){
    char *p=skipWs(text+6);
    while(idStart(*p)){
        char name[64];
        p=skipWs(readIdent(p,name,sizeof name));
        GlobVar *g=globFind(name,file);
        if(g) *g=globVars[--globVarCount];
        if(*p==',') p=skipWs(p+1);
    }
}

// for nama in ...: catat nama dan jenis isinya
static void forNote(int pc, char *text, Func *f, ST it, CheckTab *tb){
    char *p=skipWs(text+3);
    if(!idStart(*p)) return;
    char name[64];
    p=skipWs(readIdent(p,name,sizeof name));
    if(!startsWord(p,"in")) return;
    ST elem=forElemST(it);
    globalNote(pc,f,name,elem,1);
    // jenis variabel loop ikut diketahui untuk baris di dalam loop
    if(stKnown(elem) && !tabGet(tb,name)) tabPut(tb,name,elem,0);
}

// cek baris start..stop-1; baseIndent = indent baris paling luar scope ini.
// f: fungsi yang sedang dicek (untuk return), NULL di luar fungsi
static void checkRange(int start, int stop, int baseIndent, Func *f, CheckTab *tb){
    for(int pc=start; pc<stop; ){
        char *text=lines[pc];
        int top=indents[pc]==baseIndent;
        curPc=pc;
        if(!stmtKind[pc]) stmtKind[pc]=classify(pc);
        switch(stmtKind[pc]){
        case S_BE:
            beName[0]=0;
            checkBe(pc,text,0,top,tb);
            if(beName[0]) globalNote(pc,f,beName,beType,0);
            break;
        case S_LOCK:
            checkBe(pc,text,1,top,tb);
            break;
        case S_EX: {
            char *rest=skipWs(text+2);
            if(startsWord(rest,"be")) checkBe(pc,rest,0,top,tb);
            else if(startsWord(rest,"lock")) checkBe(pc,rest,1,top,tb);
            else { pc=blockEnd(pc); continue; }
            break;
        }
        case S_DELETE:
            checkDelete(text,tb);
            if(!f) globalDelete(text,lineFile[pc]);
            break;
        case S_PRINT:
            checkExpr(pc,text+5,'s',tb);
            break;
        case S_THROW:
            checkExpr(pc,text+5,'s',tb);
            break;
        case S_CALL:
            checkExpr(pc,text,'e',tb);
            break;
        case S_IF:
            checkExpr(pc,text+2,'e',tb);
            break;
        case S_ELSE:
            if(isElseIf(text)) checkExpr(pc,text+7,'e',tb);
            break;
        case S_LOOP:
            checkExpr(pc,text+4,'e',tb);
            break;
        case S_FOR: {
            char *in=strstr(text," in ");
            if(in) forNote(pc,text,f,checkExpr(pc,in+4,'e',tb),tb);
            break;
        }
        case S_RETURN: {
            ST g=checkExpr(pc,text+6,'s',tb);
            if(f && f->rkind!=K_ANY){
                int r=stFit(f->rkind,f->rsname,f->relem,g);
                if(r==1) nrxFailAt(pc,"fungsi %s harus mengembalikan int, bukan angka desimal; pakai int(...)",f->name);
                if(r==2) nrxFailAt(pc,"fungsi %s harus mengembalikan %s, bukan %s",f->name,f->rtype,stName(g));
            }
            break;
        }
        case S_TRY: {
            // try dan catch-nya dilewati
            int next=blockEnd(pc);
            if(next<stop && startsWord(lines[next],"catch")) next=blockEnd(next);
            pc=next;
            continue;
        }
        case S_SKIP: case S_WINDOW:
            pc=blockEnd(pc);
            continue;
        case S_FFI:
            pc=ffiEnd(pc)+1;
            continue;
        }
        pc++;
    }
}

static void checkProgram(void){
    static CheckTab tb;
    globVarCount=0;
    // tingkat paling luar tiap file; tabelnya dimulai lagi untuk tiap file
    for(int pc=0; pc<lineCount; ){
        int file=lineFile[pc];
        int end=pc;
        while(end<lineCount && lineFile[end]==file) end++;
        tb.n=0;
        checkRange(pc,end,0,NULL,&tb);
        pc=end;
    }
    // isi tiap fungsi, method, dan init: parameter bertipe dan own sudah diketahui
    for(int i=0;i<funcCount;i++){
        Func *f=&funcs[i];
        int end=blockEnd(f->pc);
        if(end<=f->pc+1) continue;
        tb.n=0;
        if(f->ownerType>=0) tabPut(&tb,"own",st(K_STRUCT,K_ANY,structs[f->ownerType].name),0);
        for(int j=0;j<f->nparams;j++){
            if(f->pkind[j]!=K_ANY) tabPut(&tb,f->params[j],st(f->pkind[j],f->pelem[j],f->psname[j]),0);
        }
        checkRange(f->pc+1,end,indents[f->pc+1],f,&tb);
    }
    curPc=0;
}

#include "nrx_rt.c"
#include "nrx_build.c"

// dipanggil main() di main.c; program hasil compile memakai rt_main sebagai gantinya
int nrxMain(int argc,char **argv){
    if(argc>=2 && !strcmp(argv[1],"build")) return buildMain(argc,argv);
    if(argc<3 || strcmp(argv[1],"run")){
        printf("usage: %s run file.nrxl\n       %s build file.nrxl [-o file.exe]\n",argv[0],argv[0]);
        return 1;
    }
    srcFile=argv[2];
    detectErrLang(srcFile);
    loadFile(srcFile,-1,0);
    checkImNames();

    retVal=valStr("");
    prepass();
    checkProgram();
    int rc=execRange(0,lineCount);
    if(rc==RC_BREAK || rc==RC_CONTINUE) noLoop(rc);
    if(windowLib) nrxWindowFinish();
    return 0;
}
