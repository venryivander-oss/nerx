#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "nrx_crudfile.h"

static const char *names[]={
    "createFile","readFile","readLines","writeFile","writeLines",
    "appendFile","appendLine","deleteFile","fileExists",
    "listFiles","readRange","findInFile","replaceInFile"
};

int nrxFileOwns(const char *name){
    for(size_t i=0;i<sizeof names/sizeof names[0];i++){
        if(strcmp(name,names[i])==0) return 1;
    }
    return 0;
}

static int exists(const char *path){
    FILE *f=fopen(path,"r");
    if(!f) return 0;
    fclose(f);
    return 1;
}

static char *readAll(const char *name, const char *path){
    FILE *f=fopen(path,"r");
    if(!f) nrxFail("%s: file %s tidak ditemukan atau tidak bisa dibuka",name,path);
    size_t cap=4096, len=0;
    char *buf=malloc(cap);
    size_t got;
    while((got=fread(buf+len,1,cap-len-1,f))>0){
        len+=got;
        if(cap-len-1==0){
            cap*=2;
            buf=realloc(buf,cap);
        }
    }
    buf[len]=0;
    fclose(f);
    return buf;
}

// isi file apa adanya (byte), untuk replaceInFile supaya akhir baris tidak berubah
static char *readRaw(const char *name, const char *path, size_t *outLen){
    FILE *f=fopen(path,"rb");
    if(!f) nrxFail("%s: file %s tidak ditemukan atau tidak bisa dibuka",name,path);
    fseek(f,0,SEEK_END);
    long sz=ftell(f);
    fseek(f,0,SEEK_SET);
    char *buf=malloc(sz+1);
    size_t got=fread(buf,1,sz,f);
    buf[got]=0;
    fclose(f);
    *outLen=got;
    return buf;
}

// \n -> \r\n (teks baru, yang lama dibebaskan pemanggil)
static char *crlf(const char *s){
    size_t n=0;
    for(const char *p=s;*p;p++) n+=*p=='\n' ? 2 : 1;
    char *o=malloc(n+1), *w=o;
    for(const char *p=s;*p;p++){
        if(*p=='\n') *w++='\r';
        *w++=*p;
    }
    *w=0;
    return o;
}

static FILE *openWrite(const char *name, const char *path, const char *mode){
    FILE *f=fopen(path,mode);
    if(!f) nrxFail("%s: file %s tidak bisa ditulis",name,path);
    return f;
}

Value nrxFileCall(const char *name, Value *a, int n){
    int want=(!strcmp(name,"writeFile") || !strcmp(name,"writeLines")
           || !strcmp(name,"appendFile") || !strcmp(name,"appendLine")
           || !strcmp(name,"findInFile")) ? 2
           : (!strcmp(name,"readRange") || !strcmp(name,"replaceInFile")) ? 3 : 1;
    if(n!=want) nrxFail("%s butuh %d argumen, tapi diberi %d",name,want,n);
    char *path=valToStr(a[0]);
    Value r;

    if(!strcmp(name,"fileExists")){
        r=valBool(exists(path));
    }
    else if(!strcmp(name,"createFile")){
        if(exists(path)) r=valBool(0);
        else {
            fclose(openWrite(name,path,"w"));
            r=valBool(1);
        }
    }
    else if(!strcmp(name,"readFile")){
        r=valStrOwn(readAll(name,path));
    }
    else if(!strcmp(name,"readLines")){
        char *all=readAll(name,path);
        List *l=listNew();
        char *line=all;
        while(*line){
            char *nl=strchr(line,'\n');
            if(nl) *nl=0;
            listPush(l,valStr(line));
            if(!nl) break;
            line=nl+1;
        }
        free(all);
        r=valList(l);
    }
    else if(!strcmp(name,"writeLines")){
        if(a[1].type!=T_LIST) nrxFail("writeLines butuh list sebagai argumen kedua");
        FILE *f=openWrite(name,path,"w");
        for(int i=0;i<a[1].list->count;i++){
            char *s=valToStr(a[1].list->items[i]);
            fprintf(f,"%s\n",s);
            free(s);
        }
        fclose(f);
        r=valBool(1);
    }
    else if(!strcmp(name,"listFiles")){
        // nama file dan folder di dalam folder; folder diberi akhiran /
        List *l=listNew();
        #ifdef _WIN32
        char pat[MAX_PATH+4];
        snprintf(pat,sizeof pat,"%s\\*",path);
        WIN32_FIND_DATAA d;
        HANDLE h=FindFirstFileA(pat,&d);
        if(h==INVALID_HANDLE_VALUE) nrxFail("listFiles: folder %s tidak ditemukan",path);
        do {
            if(!strcmp(d.cFileName,".") || !strcmp(d.cFileName,"..")) continue;
            char nm[MAX_PATH+2];
            snprintf(nm,sizeof nm,"%s%s",d.cFileName,(d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? "/" : "");
            listPush(l,valStr(nm));
        } while(FindNextFileA(h,&d));
        FindClose(h);
        #else
        nrxFail("%s hanya didukung di Windows",name);
        #endif
        r=valList(l);
    }
    else if(!strcmp(name,"readRange") || !strcmp(name,"findInFile")){
        // readRange(path, mulai, jumlah): list baris, baris pertama = 1
        // findInFile(path, teks): list nomor baris yang berisi teks
        int range=!strcmp(name,"readRange");
        int from=0, cnt=0;
        char *want2=NULL;
        if(range){
            if(a[1].type!=T_NUM || a[2].type!=T_NUM) nrxFail("readRange butuh angka: readRange(path, mulai, jumlah)");
            from=(int)a[1].num;
            cnt=(int)a[2].num;
        }
        else want2=valToStr(a[1]);
        char *all=readAll(name,path);
        List *l=listNew();
        char *line=all;
        int no=1;
        while(*line){
            char *nl=strchr(line,'\n');
            if(nl) *nl=0;
            size_t ln=strlen(line);
            if(ln && line[ln-1]=='\r') line[ln-1]=0;
            if(range){
                if(no>=from && no<from+cnt) listPush(l,valStr(line));
            }
            else if(strstr(line,want2)) listPush(l,valNum(no));
            if(!nl || (range && no>=from+cnt)) break;
            line=nl+1;
            no++;
        }
        free(all);
        free(want2);
        r=valList(l);
    }
    else if(!strcmp(name,"replaceInFile")){
        // ganti semua teks lama dengan teks baru, hasilnya jumlah yang diganti
        char *old=valToStr(a[1]), *nw=valToStr(a[2]);
        if(!old[0]) nrxFail("replaceInFile: teks yang dicari tidak boleh kosong");
        size_t len;
        char *all=readRaw(name,path,&len);
        // file CRLF: \n di teks lama/baru dibaca sebagai \r\n, sama dengan readFile
        if(strstr(all,"\r\n")){
            char *t;
            if(!strchr(old,'\r') && strchr(old,'\n')){ t=crlf(old); free(old); old=t; }
            if(!strchr(nw,'\r') && strchr(nw,'\n')){ t=crlf(nw); free(nw); nw=t; }
        }
        size_t ol=strlen(old), nl2=strlen(nw);
        int count=0;
        for(char *p=strstr(all,old);p;p=strstr(p+ol,old)) count++;
        if(count){
            char *out=malloc(len+count*(nl2+1)+1), *o=out, *src=all;
            for(char *p=strstr(src,old);p;p=strstr(src,old)){
                memcpy(o,src,p-src); o+=p-src;
                memcpy(o,nw,nl2); o+=nl2;
                src=p+ol;
            }
            strcpy(o,src);
            FILE *f=openWrite(name,path,"wb");
            fputs(out,f);
            fclose(f);
            free(out);
        }
        free(all);
        free(old);
        free(nw);
        r=valNum(count);
    }
    else if(!strcmp(name,"deleteFile")){
        r=valBool(remove(path)==0);
    }
    else {
        // writeFile, appendFile, appendLine
        char *text=valToStr(a[1]);
        // appendLine selalu mulai di baris baru, walau file belum diakhiri \n
        int needNl=0;
        if(!strcmp(name,"appendLine")){
            FILE *rf=fopen(path,"rb");
            if(rf){
                if(fseek(rf,-1,SEEK_END)==0 && fgetc(rf)!='\n') needNl=1;
                fclose(rf);
            }
        }
        FILE *f=openWrite(name,path,!strcmp(name,"writeFile") ? "w" : "a");
        if(needNl) fputc('\n',f);
        fputs(text,f);
        if(!strcmp(name,"appendLine")) fputc('\n',f);
        fclose(f);
        free(text);
        r=valBool(1);
    }
    free(path);
    return r;
}
