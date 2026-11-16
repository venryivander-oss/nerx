// nrx_rt.c: perintah, try/catch, dan awal program untuk kode C hasil "nrxlang build".
// Dimasukkan (#include) di akhir nrxlang.c karena memakai bagian interpreter yang static.
// Fungsi rt_* untuk ekspresi ada di nrxlang.c, di bagian "menjalankan pohon".

// teks yang tidak bisa dibaca saat build: dihitung saat jalan, supaya errornya
// muncul di tempat yang sama dengan interpreter (mis. di dalam try)
Value rt_eval(const char *src, int seq){
    return seq ? evalSeq(src) : evalExpr(src);
}

// perintah satu baris (be, lock, ex, delete, ask, wait, errlang) dijalankan persis seperti
// interpreter; pre = nilai kanan be yang sudah dihitung kode C (NULL kalau tidak ada)
void rt_stmt(int pc, Value *pre){
    bePreset=pre;
    execRange(pc,pc+1);
    bePreset=NULL;
}

void rt_print(Value v){
    char *s=valToStr(v);
    valFree(&v);
    printf("%s\n",s);
    free(s);
}

void rt_throw(Value v){
    char *msg=valToStr(v);
    valFree(&v);
    nrxFail("%s",msg);
}

void rt_return_check(void){
    if(depth==0) nrxFail("return hanya boleh di dalam fungsi");
}

void rt_return(Value v){
    valFree(&retVal);
    retVal=v;
}

void rt_setvar(const char *name, Value v){
    setVar(name,v);
}

double rt_sqrt(double d){
    if(d<0) nrxFail("sqrt: tidak bisa akar dari angka negatif");
    return sqrt(d);
}

void rt_setloopvar(const char *name, Value v){
    setLoopVar(name,v);
}

// panggil fungsi buatan sendiri langsung (nama sudah dicari saat build, sama seperti callNamed)
Value rt_callf(int fpc, Value *a, int n){
    Value r=callFunc(fpc,a,n);
    freeArgs(a,n);
    return r;
}

// ---- variabel C (int/double/bool disimpan sebagai double) ----

// nilai baru untuk variabel C: dicek persis seperti be ke variabel biasa (pesan error sama)
double rt_unbox(Value v, int kind, const char *name){
    // jalan cepat: angka yang pasti cocok (aturan fitKind), tanpa membuat Var
    if(v.type==T_NUM && (kind==K_DOUBLE || kind==K_FLOAT || (kind==K_INT && !v.sub))) return v.num;
    Var tmp;
    memset(&tmp,0,sizeof tmp);
    snprintf(tmp.name,sizeof tmp.name,"%s",name);
    tmp.kind=kind;
    tmp.elem=K_ANY;
    checkVarKind(&tmp,&v);
    double r=kind==K_BOOL ? valTruthy(v) : v.num;
    valFree(&v);
    return r;
}

// be x op nilai: x variabel C (lk = jenisnya), nilai kanan Value. Angka yang hasilnya pasti
// cocok dihitung langsung (aturan sama dengan binop + fitKind); selain itu lewat rt_bin.
double rt_numop(double l, int lk, int op, Value r, int kind, const char *name){
    if(r.type==T_NUM && lk!=K_BOOL && (r.num!=0 || op=='+' || op=='-' || op=='*')){
        int dec=lk==K_DOUBLE || r.sub || op=='/';
        if(!dec || kind!=K_INT){
            double x=r.num;
            return op=='+' ? l+x : op=='-' ? l-x : op=='*' ? l*x : op=='/' ? l/x : fmod(l,x);
        }
    }
    Value a=lk==K_INT ? valNum(l) : lk==K_BOOL ? valBool(l!=0) : valDec(l);
    return rt_unbox(rt_bin(a,op,r),kind,name);
}

Value rt_pval(int i){
    return valCopy(frames[depth-1].vars[i].val);
}

void rt_vset(RtVar *r, Value v, const char *name){
    Var t;
    memset(&t,0,sizeof t);
    snprintf(t.name,sizeof t.name,"%s",name);
    t.kind=r->kind;
    t.elem=r->elem;
    t.sname=r->sname;
    checkVarKind(&t,&v);
    r->kind=t.kind;
    r->elem=t.elem;
    r->sname=t.sname;
    valFree(&r->val);
    r->val=v;
}

// ---- be nama[i]... = nilai, index dihitung kode C (sama dengan execBe) ----

Value *rt_root(const char *name){
    Var *v=findVarPtr(name);
    if(!v) unknownVar(name);
    if(v->locked) nrxFail("%s di-lock, isinya tidak bisa diubah",name);
    return &v->val;
}

void rt_lockchk(Value *base, const char *name){
    stepLockCheck(base,name);
}

Value *rt_step(Value *base, Value idx, int last, const char *name){
    return stepIdx(base,idx,last,name);
}

// simpan v di t (hasil rt_step terakhir); op '=' atau + - * / %
void rt_put(Value *t, int op, Value v, const char *name){
    int created=lastCreated;
    if(op!='='){
        Value cur=valCopy(*t);
        if(created){
            int k=lastBase->map->elem;
            if(k==K_ANY || k==K_MIXED) k=valKind(v);
            if(k==K_INT || k==K_DOUBLE || k==K_FLOAT || k==K_STRING || k==K_BOOL){
                valFree(&cur);
                cur=defaultKind(k,K_ANY,NULL,NULL,"");
            }
        }
        Value r=binop(cur,(char)op,v);
        valFree(&cur);
        valFree(&v);
        v=r;
    }
    storeInto(lastBase,t,created,v,name);
}

// hasil yang jenisnya sudah pasti saat build (mis. fungsi bertipe int)
double rt_unum(Value v){
    double r=v.type==T_NUM ? v.num : valTruthy(v);
    valFree(&v);
    return r;
}

// parameter ke-i fungsi yang sedang jalan
double rt_param(int i){
    Value v=frames[depth-1].vars[i].val;
    return v.type==T_NUM ? v.num : valTruthy(v);
}

// fungsi C asli (lihat nrx_build.c): sama dengan callFuncOwn, tapi tanpa frame berisi variabel.
// depth tetap dihitung, jadi batas rekursi dan pembersihan saat catch sama
void rt_enter(void){
    if(depth>=200) nrxFail("fungsi memanggil dirinya terlalu dalam");
    frames[depth].count=0;
    depth++;
}

void rt_leave(void){
    depth--;
}

void rt_noreturn(int fpc){
    Func *f=funcAt(fpc);
    nrxFail("fungsi %s harus mengembalikan %s, tapi tidak ada return",f->name,f->rtype);
}

void rt_div0(void){
    nrxFail("tidak bisa dibagi dengan nol");
}

// for i in range(...): batasnya saja, tanpa membuat list; errornya sama dengan range()
void rt_range(Value *a, int n, double *from, double *to, double *step){
    rangeArgs(a,n,from,to,step);
    freeArgs(a,n);
    // range() menolak lebih dari 10 juta isi; hitung persis hanya kalau mendekati batas itu
    if((*to-*from)/ *step>9990000){
        long c=0;
        for(double x=*from; *step>0 ? x<*to : x>*to; x+=*step){
            if(++c>10000000) nrxFail("range terlalu besar");
        }
    }
}

void rt_release(List *l){
    listRelease(l);
}

// option "1", "2": v cocok dengan salah satu isi options (list)
int rt_opt_match(Value *v, Value options){
    if(options.list->count==0) nrxFail("option butuh nilai, contoh: option \"1\"");
    int found=0;
    for(int k=0;k<options.list->count && !found;k++){
        if(compareVals(options.list->items[k],*v)==0) found=1;
    }
    valFree(&options);
    return found;
}

// error susunan blok yang ditemukan saat build; munculnya baru saat baris itu jalan,
// sama seperti interpreter. which: lihat RT_FAIL_* di nrxrt.h
void rt_fail(int pc, int which){
    curPc=pc;
    switch(which){
        case RT_FAIL_OPT_INDENT: nrxFail("isi options harus sejajar (indent sama)");
        case RT_FAIL_OPT_ITEM: nrxFail("di dalam options hanya boleh option atau else");
        case RT_FAIL_FOR: nrxFail("cara menulis: for nama in list");
        case RT_FAIL_TRY: nrxFail("try harus diikuti catch dengan indent yang sama");
        case RT_FAIL_CATCH: nrxFail("catch harus berada tepat setelah blok try");
        default: nrxFail("option harus berada di dalam blok options");
    }
}

void rt_unknown(int pc){
    curPc=pc;
    nrxFail("perintah tidak dikenal: %s",lines[pc]);
}

jmp_buf *rt_try_push(void){
    if(tryTop>=64) nrxFail("try bersarang terlalu dalam");
    return &tryBufs[tryTop++];
}

void rt_try_pop(int n){
    tryTop-=n;
}

int rt_depth(void){
    return depth;
}

// sampai di catch: bereskan fungsi yang terpotong error, lalu isi variabel catch
void rt_catch(int catchPc, int savedDepth){
    tryTop--;
    bePreset=NULL;
    while(depth>savedDepth){
        depth--;
        for(int i=0;i<frames[depth].count;i++) valFree(&frames[depth].vars[i].val);
    }
    curPc=catchPc;
    char *p=skipWs(lines[catchPc]+5);
    if(*p){
        if(!isIdent(p)) nrxFail("cara menulis: catch atau catch namaVariabel");
        setVar(p,valStr(tryMsg));
    }
}

void rt_body(int pc, int (*fn)(void)){
    compiledBody[pc]=fn;
}

// main program hasil compile: file sumber dibaca dari teks yang ditanam, lalu sama
// dengan interpreter, hanya isi programnya yang sudah jadi kode C
int rt_main(const char *mainPath, const char **names, const char **texts, int n, int (*top)(void)){
    embedNames=names;
    embedTexts=texts;
    embedCount=n;
    srcFile=(char*)mainPath;
    detectErrLang(srcFile);
    loadFile(srcFile,-1,0);
    checkImNames();
    retVal=valStr("");
    prepass();
    int rc=top();
    if(rc==RC_BREAK || rc==RC_CONTINUE) noLoop(rc);
    if(windowLib) nrxWindowFinish();
    return 0;
}
