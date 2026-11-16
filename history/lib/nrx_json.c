#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "nrx_json.h"

int nrxJsonOwns(const char *name){
    return !strcmp(name,"toJson") || !strcmp(name,"fromJson");
}

// ---------------- toJson ----------------

typedef struct {
    char *s;
    size_t len, cap;
} Buf;

static void put(Buf *b, const char *s, size_t n){
    if(b->len+n+1>b->cap){
        while(b->len+n+1>b->cap) b->cap=b->cap ? b->cap*2 : 256;
        b->s=realloc(b->s,b->cap);
    }
    memcpy(b->s+b->len,s,n);
    b->len+=n;
    b->s[b->len]=0;
}

static void puts_(Buf *b, const char *s){
    put(b,s,strlen(s));
}

static void putString(Buf *b, const char *s){
    puts_(b,"\"");
    for(; *s; s++){
        unsigned char c=*s;
        if(c=='"') puts_(b,"\\\"");
        else if(c=='\\') puts_(b,"\\\\");
        else if(c=='\n') puts_(b,"\\n");
        else if(c=='\r') puts_(b,"\\r");
        else if(c=='\t') puts_(b,"\\t");
        else if(c<0x20){
            char u[8];
            snprintf(u,sizeof u,"\\u%04x",c);
            puts_(b,u);
        }
        else put(b,(const char*)&c,1);
    }
    puts_(b,"\"");
}

static void newline(Buf *b, int pretty, int depth){
    if(!pretty) return;
    puts_(b,"\n");
    for(int i=0;i<depth;i++) puts_(b,"  ");
}

static void writeValue(Buf *b, Value v, int pretty, int depth){
    if(depth>100) nrxFail("toJson: data terlalu bertingkat (mungkin list/map berisi dirinya sendiri)");
    if(v.type==T_NUM){
        char *s=valToStr(v);
        puts_(b,s);
        free(s);
    } else if(v.type==T_STR){
        // true/false di nrxl berupa teks, di JSON jadi boolean
        if(!strcmp(v.str,"true") || !strcmp(v.str,"false")) puts_(b,v.str);
        else putString(b,v.str);
    } else if(v.type==T_ARR){
        char *s=valToStr(v);
        puts_(b,s);
        free(s);
    } else if(v.type==T_LIST){
        if(v.list->count==0){
            puts_(b,"[]");
            return;
        }
        puts_(b,"[");
        for(int i=0;i<v.list->count;i++){
            if(i>0) puts_(b,pretty ? "," : ", ");
            newline(b,pretty,depth+1);
            writeValue(b,v.list->items[i],pretty,depth+1);
        }
        newline(b,pretty,depth);
        puts_(b,"]");
    } else {
        if(v.map->count==0){
            puts_(b,"{}");
            return;
        }
        puts_(b,"{");
        for(int i=0;i<v.map->count;i++){
            if(i>0) puts_(b,pretty ? "," : ", ");
            newline(b,pretty,depth+1);
            putString(b,v.map->keys[i]);
            puts_(b,": ");
            writeValue(b,v.map->vals[i],pretty,depth+1);
        }
        newline(b,pretty,depth);
        puts_(b,"}");
    }
}

// ---------------- fromJson ----------------

static const char *jsrc;
static const char *jp;

static void jfail(const char *msg){
    nrxFail("fromJson: %s (di karakter ke-%d)",nrxTr(msg),(int)(jp-jsrc)+1);
}

static void skipWs(void){
    while(*jp==' ' || *jp=='\t' || *jp=='\n' || *jp=='\r') jp++;
}

// tambahkan satu kode Unicode sebagai UTF-8
static void putUtf8(Buf *b, unsigned cp){
    char o[4];
    int n;
    if(cp<0x80){ o[0]=cp; n=1; }
    else if(cp<0x800){ o[0]=0xC0|(cp>>6); o[1]=0x80|(cp&0x3F); n=2; }
    else if(cp<0x10000){ o[0]=0xE0|(cp>>12); o[1]=0x80|((cp>>6)&0x3F); o[2]=0x80|(cp&0x3F); n=3; }
    else { o[0]=0xF0|(cp>>18); o[1]=0x80|((cp>>12)&0x3F); o[2]=0x80|((cp>>6)&0x3F); o[3]=0x80|(cp&0x3F); n=4; }
    put(b,o,n);
}

static unsigned hex4(void){
    unsigned v=0;
    for(int i=0;i<4;i++){
        char c=jp[i];
        v<<=4;
        if(c>='0' && c<='9') v|=c-'0';
        else if(c>='a' && c<='f') v|=c-'a'+10;
        else if(c>='A' && c<='F') v|=c-'A'+10;
        else jfail("kode \\u harus 4 digit heksadesimal");
    }
    jp+=4;
    return v;
}

static char *readString(void){
    Buf b={0};
    put(&b,"",0);
    jp++;
    while(*jp!='"'){
        if(!*jp) jfail("teks belum ditutup dengan \"");
        if(*jp=='\\'){
            jp++;
            char e=*jp++;
            if(e=='n') puts_(&b,"\n");
            else if(e=='t') puts_(&b,"\t");
            else if(e=='r') puts_(&b,"\r");
            else if(e=='b') puts_(&b,"\b");
            else if(e=='f') puts_(&b,"\f");
            else if(e=='"' || e=='\\' || e=='/') put(&b,&e,1);
            else if(e=='u'){
                unsigned cp=hex4();
                // pasangan surrogate untuk karakter di luar BMP (mis. emoji)
                if(cp>=0xD800 && cp<=0xDBFF && jp[0]=='\\' && jp[1]=='u'){
                    jp+=2;
                    unsigned lo=hex4();
                    cp=0x10000+((cp-0xD800)<<10)+(lo-0xDC00);
                }
                putUtf8(&b,cp);
            }
            else jfail("escape tidak dikenal");
        } else {
            put(&b,jp,1);
            jp++;
        }
    }
    jp++;
    return b.s;
}

static Value readValue(int depth){
    if(depth>200) jfail("data terlalu bertingkat");
    skipWs();
    char c=*jp;
    if(c=='{'){
        jp++;
        Map *m=mapNew();
        m->elem=K_MIXED;   // data JSON boleh campur
        skipWs();
        if(*jp=='}'){
            jp++;
            return valMap(m);
        }
        for(;;){
            skipWs();
            if(*jp!='"') jfail("key objek harus teks dalam tanda kutip");
            char *key=readString();
            skipWs();
            if(*jp!=':') jfail("kurang tanda : setelah key");
            jp++;
            mapSet(m,key,readValue(depth+1));
            free(key);
            skipWs();
            if(*jp==','){
                jp++;
                continue;
            }
            if(*jp=='}'){
                jp++;
                return valMap(m);
            }
            jfail("kurang tanda , atau }");
        }
    }
    if(c=='['){
        jp++;
        List *l=listNew();
        l->elem=K_MIXED;
        skipWs();
        if(*jp==']'){
            jp++;
            return valList(l);
        }
        for(;;){
            listPush(l,readValue(depth+1));
            skipWs();
            if(*jp==','){
                jp++;
                continue;
            }
            if(*jp==']'){
                jp++;
                return valList(l);
            }
            jfail("kurang tanda , atau ]");
        }
    }
    if(c=='"') return valStrOwn(readString());
    if(!strncmp(jp,"true",4)){
        jp+=4;
        return valBool(1);
    }
    if(!strncmp(jp,"false",5)){
        jp+=5;
        return valBool(0);
    }
    if(!strncmp(jp,"null",4)){
        jp+=4;
        return valStr("");
    }
    if(c=='-' || isdigit((unsigned char)c)){
        char *end;
        double d=strtod(jp,&end);
        if(end==jp) jfail("angka tidak valid");
        // 2.0 dan 1e3 tetap desimal
        int dec=0;
        for(const char *q=jp; q<end; q++) if(*q=='.' || *q=='e' || *q=='E') dec=1;
        jp=end;
        return dec ? valDec(d) : valNum(d);
    }
    if(!c) jfail("teks JSON berakhir terlalu cepat");
    jfail("karakter tidak dikenal");
    return valStr("");
}

Value nrxJsonCall(const char *name, Value *a, int n){
    if(!strcmp(name,"toJson")){
        if(n!=1 && n!=2) nrxFail("toJson butuh 1 atau 2 argumen: toJson(data) atau toJson(data, true)");
        Buf b={0};
        put(&b,"",0);
        writeValue(&b,a[0],n==2 && valTruthy(a[1]),0);
        return valStrOwn(b.s);
    }
    if(n!=1) nrxFail("fromJson butuh 1 argumen: fromJson(teks)");
    char *text=valToStr(a[0]);
    jsrc=jp=text;
    Value v=readValue(0);
    skipWs();
    if(*jp) jfail("ada sisa teks setelah data JSON");
    free(text);
    return v;
}
