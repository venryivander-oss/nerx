// nrx_build.c: compiler "nrxlang build file.nrxl" -> file.exe, lewat C dan gcc.
// Dimasukkan (#include) di akhir nrxlang.c karena memakai AST dan bagian interpreter.
//
// Pohon AST tiap baris ditulis jadi kode C: alur program (if, loop, for, try, options,
// return, break) jadi alur C, ekspresi jadi panggilan rt_* (nrxrt.h). Perintah satu baris
// seperti be dijalankan lewat rt_stmt dengan nilai kanan yang sudah dihitung kode C.
// Isi file .nrxl ikut ditanam di .exe, jadi im, pesan error, dan nomor baris tetap sama.
// Library window dan ffi: baris ffi sudah dibaca saat prepass (di program hasil build
// juga), baris window dijalankan rt_stmt seperti interpreter.

static FILE *cg;        // file .c yang sedang ditulis
static int cgId=0;      // nomor unik untuk nama variabel C sementara

// teks sebagai string C: "a\"b\n"
static void cgStr(const char *s){
    fputc('"',cg);
    for(const unsigned char *c=(const unsigned char*)s; *c; c++){
        if(*c=='\\' || *c=='"') fprintf(cg,"\\%c",*c);
        else if(*c=='\n') fputs("\\n",cg);
        else if(*c=='\t') fputs("\\t",cg);
        else if(*c<32 || *c==127) fprintf(cg,"\\%03o",*c);
        else if(*c=='?') fputs("\\?",cg);   // hindari trigraph ??=
        else fputc(*c,cg);
    }
    fputc('"',cg);
}

static void cgExpr(Node *x);
static char *beRhs(char *text, int kwLen);

// ---------------- variabel C ----------------
// Variabel int/double/bool yang aman disimpan sebagai variabel C biasa (double), jadi
// tidak dicari lewat nama setiap kali dipakai. Aman artinya, untuk setiap variabel:
//  - dibuat sekali: parameter bertipe, be/lock di tingkat paling luar fungsi/program sebelum
//    dipakai, atau for nama in range(...) yang hanya dipakai di dalam for itu
//  - setiap baris yang menyebut namanya ditulis jadi kode C: be/lock ke variabel itu, atau
//    ekspresi (if, loop, print, return, nilai kanan be lain, ...) yang bisa dibaca saat build
//  - tidak ada tempat lain yang bisa melihatnya lewat nama (global: tidak disebut di fungsi,
//    type, atau file lain; lokal: tidak disebut di luar fungsi, karena be di fungsi
//    mengubah global yang namanya sama)
// Kalau salah satu tidak terpenuhi, variabel itu tetap lewat nama seperti biasa.
// Nilai yang masuk dicek dengan aturan yang sama (rt_unbox), jadi pesan errornya sama.
// Variabel jenis lain (teks, list, map, struct) yang aman dengan aturan yang sama disimpan
// sebagai RtVar (Value + jenisnya); be ke variabel itu dicek rt_vset seperti setVar.

typedef struct {
    char name[64];
    int kind;       // jenis yang ditulis (K_INT, K_DOUBLE, K_FLOAT, K_BOOL)
    int param;      // >0: parameter, index-nya di frame + 1
    int declPc;     // baris yang membuatnya
    int locked;
    int forDecl;    // dibuat oleh for nama in range(...)
    int alive;
    int boxed;      // RtVar vv_nama (bukan double)
    int elem;       // boxed: jenis isi list/map yang ditulis
} NVar;

static NVar nv[256];
static void cgNfCall(Func *f, Node *x);
static Func *nfCallee(Node *x);
static int nvCount;
static int nvOwner;       // -1 program utama, selain itu baris func
static int nvVolatile;    // ada try di fungsi ini: nilai harus tetap benar setelah longjmp
static int *lineOwner;    // tiap baris: -1 program utama, -2 isi type, selain itu baris func

static Node *cgParse(const char *src, char mode);

static NVar *nvAny(const char *name){
    for(int i=0;i<nvCount;i++) if(nv[i].alive && !strcmp(nv[i].name,name)) return &nv[i];
    return NULL;
}

// hanya variabel C angka/bool (double)
static NVar *nvFind(const char *name){
    NVar *v=nvAny(name);
    return v && !v->boxed ? v : NULL;
}

// float dihitung sama dengan double
static int numK(int k){
    return k==K_FLOAT ? K_DOUBLE : k;
}

static int isNumK(int k){
    return k==K_INT || k==K_DOUBLE;
}

// jenis yang bisa disimpan: int <- int; double/float <- int atau double; bool <- bool
static int fitsK(int want, int got){
    want=numK(want);
    if(want==K_DOUBLE) return isNumK(got);
    return got && want==got;
}

static int skind(Node *x);
static NVar *nvAny(const char *name);

// a[i]: a variabel C berjenis array dan i angka
static NVar *arrIndexVar(Node *x){
    if(x->kind!=N_INDEX || x->kids[0]->kind!=N_VAR) return NULL;
    NVar *v=nvAny(x->kids[0]->name);
    if(!v || !v->boxed || v->kind!=K_ARRAY) return NULL;
    int k=skind(x->kids[1]);
    return k==K_INT || k==K_DOUBLE ? v : NULL;
}

// fungsi matematika bawaan yang bisa jadi C langsung kalau semua argumennya angka:
// jenis hasilnya (K_INT/K_DOUBLE), atau 0. Hasilnya sama dengan callNamed.
static int mathKind(Node *x){
    static const char *names[]={"sqrt","sin","cos","tan","atan2","pi","abs","floor","ceil"};
    static const int nargs[]={1,1,1,1,2,0,1,1,1};
    if(userFuncFor(x->name) || findFfi(x->name)) return 0;
    for(int i=0;i<9;i++){
        if(strcmp(x->name,names[i])) continue;
        if(x->n!=nargs[i]) return 0;
        for(int j=0;j<x->n;j++) if(!isNumK(skind(x->kids[j]))) return 0;
        if(i==6) return skind(x->kids[0]);
        return i>=7 ? K_INT : K_DOUBLE;
    }
    return 0;
}

// jenis hasil ekspresi yang pasti saat build (K_INT, K_DOUBLE, K_BOOL), 0 = belum pasti
static int skind(Node *x){
    switch(x->kind){
    case N_LIT:
        if(x->lit.type==T_NUM) return x->lit.sub ? K_DOUBLE : K_INT;
        return x->lit.sub ? K_BOOL : 0;
    case N_VAR: {
        NVar *v=nvFind(x->name);
        return v ? numK(v->kind) : 0;
    }
    case N_BIN: {
        int a=skind(x->kids[0]), b=skind(x->kids[1]);
        if(!isNumK(a) || !isNumK(b)) return 0;
        if(x->op=='/') return K_DOUBLE;
        return a==K_DOUBLE || b==K_DOUBLE ? K_DOUBLE : K_INT;
    }
    case N_NEG: {
        int a=skind(x->kids[0]);
        return isNumK(a) ? a : 0;
    }
    case N_CMP: case N_IN: case N_NOT: case N_AND: case N_OR:
        return K_BOOL;
    case N_INDEX:
        return arrIndexVar(x) ? K_DOUBLE : 0;
    case N_CALL: {
        Func *f=userFuncFor(x->name);
        if(!f) return mathKind(x);
        int k=numK(f->rkind);
        return isNumK(k) || k==K_BOOL ? k : 0;
    }
    }
    return 0;
}

// ada variabel C di dalam ekspresi ini?
static int hasNative(Node *x){
    if(x->kind==N_VAR) return nvFind(x->name)!=NULL;
    if(arrIndexVar(x)) return 1;
    for(int i=0;i<x->n;i++) if(hasNative(x->kids[i])) return 1;
    return 0;
}

// node ini bisa dihitung langsung dengan double C (anak-anaknya lewat cgNum)
static int nativeOk(Node *x){
    switch(x->kind){
    case N_LIT: case N_BIN: case N_NEG:
        return skind(x)!=0;
    case N_VAR:
        return nvFind(x->name)!=NULL;
    case N_INDEX:
        return arrIndexVar(x)!=NULL;
    case N_CMP: {
        int a=skind(x->kids[0]), b=skind(x->kids[1]);
        return (isNumK(a) && isNumK(b)) || (a==K_BOOL && b==K_BOOL);
    }
    case N_NOT: case N_AND: case N_OR:
        return 1;
    case N_CALL:
        return mathKind(x)!=0;
    }
    return 0;
}

static void cgDbl(double d){
    char b[64];
    snprintf(b,sizeof b,"%.17g",d);
    fputs(b,cg);
    if(!strpbrk(b,".eEn")) fputs(".0",cg);
}

static void cgExpr(Node *x);
static void cgNum(Node *x);

// ekspresi sebagai syarat C (0 atau 1)
static void cgCond(Node *x){
    if(hasNative(x) && skind(x)){
        fputs("(0!=",cg);
        cgNum(x);
        fputc(')',cg);
        return;
    }
    fputs("rt_truthy(",cg);
    cgExpr(x);
    fputc(')',cg);
}

// a op b untuk double C, sama dengan binop (bagi nol dicek setelah kedua sisi dihitung)
static void cgOpTail(int op){
    if(op=='/') fputs("if(_r==0) rt_div0(); _l/_r;",cg);
    else if(op=='%') fputs("if(_r==0) rt_div0(); fmod(_l,_r);",cg);
    else fprintf(cg,"_l%c_r;",op);
}

// nilai ekspresi sebagai double C; hanya untuk ekspresi yang skind-nya pasti
static void cgNum(Node *x){
    Func *nf=nfCallee(x);
    if(nf){
        cgNfCall(nf,x);
        return;
    }
    if(nativeOk(x)){
        switch(x->kind){
        case N_LIT:
            cgDbl(x->lit.type==T_NUM ? x->lit.num : x->lit.str[0]=='t');
            return;
        case N_VAR:
            fprintf(cg,"v_%s",x->name);
            return;
        case N_INDEX:
            fprintf(cg,"rt_arr_get(&vv_%s.val,",x->kids[0]->name);
            cgNum(x->kids[1]);
            fputc(')',cg);
            return;
        case N_BIN:
            fputs("({ double _l=",cg);
            cgNum(x->kids[0]);
            fputs("; double _r=",cg);
            cgNum(x->kids[1]);
            fputs("; ",cg);
            cgOpTail(x->op);
            fputs(" })",cg);
            return;
        case N_NEG:
            fputs("(-",cg);
            cgNum(x->kids[0]);
            fputc(')',cg);
            return;
        case N_CMP: {
            // sama dengan compareVals: (a>b)-(a<b), jadi NaN dianggap sama
            static const char *cmpC[]={"<=0",">=0","==0","!=0","<0",">0"};
            fputs("({ double _l=",cg);
            cgNum(x->kids[0]);
            fputs("; double _r=",cg);
            cgNum(x->kids[1]);
            fprintf(cg,"; (double)(((_l>_r)-(_l<_r))%s); })",cmpC[x->op]);
            return;
        }
        case N_CALL: {
            // sqrt(x), sin(x), ... langsung dengan math.h; sqrt negatif tetap error yang sama
            const char *fn=!strcmp(x->name,"sqrt") ? "rt_sqrt" : !strcmp(x->name,"abs") ? "fabs" : x->name;
            if(!strcmp(fn,"pi")){
                fputs("3.14159265358979323846",cg);
                return;
            }
            fprintf(cg,"%s(",fn);
            for(int j=0;j<x->n;j++){
                if(j) fputc(',',cg);
                cgNum(x->kids[j]);
            }
            fputc(')',cg);
            return;
        }
        case N_NOT:
            fputs("(double)!",cg);
            cgCond(x->kids[0]);
            return;
        case N_AND: case N_OR:
            fputs("(double)(",cg);
            cgCond(x->kids[0]);
            fputs(x->kind==N_AND ? " && " : " || ",cg);
            cgCond(x->kids[1]);
            fputc(')',cg);
            return;
        }
    }
    // jenisnya pasti tapi dihitung lewat Value (mis. fungsi bertipe int)
    fputs("rt_unum(",cg);
    cgExpr(x);
    fputc(')',cg);
}

// double C kembali jadi Value sesuai jenisnya; ditutup dengan ')'
static void cgBoxOpen(int k){
    fputs(k==K_INT ? "valNum(" : k==K_BOOL ? "valBool(0!=" : "valDec(",cg);
}

// ---- memeriksa variabel mana yang boleh jadi variabel C ----

// name disebut sebagai nama (bukan bagian dari kata lain, bukan .field, bukan di dalam
// "teks": teks tidak pernah dibaca sebagai nama variabel); hanya sampai end
static int mentionsUpTo(const char *t, const char *end, const char *name){
    size_t n=strlen(name);
    int inq=0;
    for(const char *p=t; *p && (!end || p<end); p++){
        if(inq){
            if(*p=='\\' && p[1]) p++;
            else if(*p=='"') inq=0;
            continue;
        }
        if(*p=='"'){
            inq=1;
            continue;
        }
        if(strncmp(p,name,n) || (p>t && (idChar(p[-1]) || p[-1]=='.')) || idChar(p[n])) continue;
        return 1;
    }
    return 0;
}

static int mentions(const char *t, const char *name){
    return mentionsUpTo(t,NULL,name);
}

// be/lock nama [tipe] [op nilai]; acc=1 kalau ada [ ] atau .field setelah nama
typedef struct {
    char name[64];
    char type[32];
    char elem[32];  // be x list int: jenis isinya
    int acc;
    char op;
    char *rhs;
} BeParts;

static int beParts(char *text, int kwLen, BeParts *b){
    memset(b,0,sizeof *b);
    char *p=skipWs(text+kwLen);
    if(!idStart(*p)) return 0;
    p=readIdent(p,b->name,sizeof b->name);
    if(*p=='[' || (*p=='.' && idStart(p[1]))){
        b->acc=1;
        return 1;
    }
    p=skipWs(p);
    if(idStart(*p)){
        p=skipWs(readIdent(p,b->type,sizeof b->type));
        if(idStart(*p)){
            if(strcmp(b->type,"list") && strcmp(b->type,"map")) return 0;
            p=skipWs(readIdent(p,b->elem,sizeof b->elem));
            if(!isType(b->elem) || idStart(*p)) return 0;
        }
    }
    if(*p=='=' && p[1]!='='){
        b->op='=';
        p++;
    } else if(*p && strchr("+-*/%",*p) && p[1]=='='){
        b->op=*p;
        p+=2;
    } else if(*p) return 0;
    if(b->op){
        p=skipWs(p);
        if(!*p) return 0;
        b->rhs=p;
    }
    return 1;
}

// be nama[i].field op nilai yang bisa ditulis langsung jadi kode C. Index hanya boleh
// ekspresi tanpa efek samping (tanpa panggilan fungsi): interpreter menghitungnya dua kali
// untuk +=, di sini sekali, dan hasilnya harus sama
typedef struct { char name[64]; Node *idx[16]; int n; char op; char *rhs; } AccBe;

static int pureIdx(Node *x){
    switch(x->kind){
    case N_LIT: case N_VAR: case N_BIN: case N_NEG: case N_INDEX: case N_FIELD: break;
    default: return 0;
    }
    for(int i=0;i<x->n;i++) if(!pureIdx(x->kids[i])) return 0;
    return 1;
}

static int accBe(char *text, int kwLen, AccBe *a){
    if(kwLen!=2) return 0;
    char *p=skipWs(text+2);
    if(!idStart(*p)) return 0;
    p=readIdent(p,a->name,sizeof a->name);
    a->n=0;
    while(*p=='[' || (*p=='.' && idStart(p[1]))){
        if(a->n>=16) return 0;
        char buf[256];
        if(*p=='['){
            int d=0;
            char *q=p;
            for(; *q; q++){
                if(*q=='[') d++;
                else if(*q==']' && --d==0) break;
            }
            if(!*q || q-p-1>=(int)sizeof buf) return 0;
            snprintf(buf,sizeof buf,"%.*s",(int)(q-p-1),p+1);
            p=q+1;
        } else {
            char field[64];
            p=readIdent(p+1,field,sizeof field);
            snprintf(buf,sizeof buf,"\"%s\"",field);
        }
        Node *x=cgParse(buf,'e');
        if(!x || !pureIdx(x)) return 0;
        a->idx[a->n++]=x;
    }
    if(!a->n) return 0;
    p=skipWs(p);
    if(*p=='=' && p[1]!='='){
        a->op='=';
        p++;
    } else if(*p && strchr("+-*/%",*p) && p[1]=='='){
        a->op=*p;
        p+=2;
    } else return 0;
    p=skipWs(p);
    if(!*p || startsWord(p,"ask") || !strcmp(p,"[]") || !strcmp(p,"{}")) return 0;
    a->rhs=p;
    return cgParse(p,'s')!=NULL;
}

// jenis yang boleh untuk variabel C
static int nativeKind(int k){
    return k==K_INT || k==K_DOUBLE || k==K_FLOAT || k==K_BOOL;
}

// for nama in range(...): pohon range-nya kalau bisa dijalankan tanpa list, selain itu NULL
static Node *forRange(char *text, char *var, int size){
    char *p=skipWs(text+3);
    if(!idStart(*p)) return NULL;
    p=skipWs(readIdent(p,var,size));
    if(!startsWord(p,"in")) return NULL;
    Node *it=cgParse(p+2,'e');
    if(!it || it->kind!=N_CALL || strcmp(it->name,"range") || it->n<1 || it->n>3) return NULL;
    return it;
}

// baris option a, b sebagai list [a, b]
static Node *optionList(char *text){
    char *buf=malloc(strlen(text)+3);
    sprintf(buf,"[%s]",text+6);
    Node *x=cgParse(buf,'e');
    free(buf);
    return x;
}

// baris pc yang menyebut v tetap benar kalau v jadi variabel C?
static int lineOk(int pc, NVar *v){
    char *t=lines[pc];
    curPc=pc;
    if(!stmtKind[pc]) stmtKind[pc]=classify(pc);
    switch(stmtKind[pc]){
    case S_BE: case S_LOCK: {
        int kw=stmtKind[pc]==S_BE ? 2 : 4;
        AccBe a;
        if(accBe(t,kw,&a)){
            // semua bagiannya ditulis kode C; variabel angka tidak punya Value untuk diubah
            return v->boxed || strcmp(a.name,v->name);
        }
        BeParts b;
        if(!beParts(t,kw,&b)) return 0;
        if(!strcmp(b.name,v->name) && !b.acc){
            if(b.rhs && (startsWord(b.rhs,"ask") || !strcmp(b.rhs,"[]") || !strcmp(b.rhs,"{}"))) return 0;
            Node *r=b.rhs ? cgParse(b.rhs,'s') : NULL;
            if(b.rhs && !r) return 0;
            if(pc==v->declPc){
                if(b.rhs && mentions(b.rhs,v->name)) return 0;
                // tanpa tipe, jenisnya diambil dari nilai: harus pasti saat build
                if(v->boxed) return kw==2 && b.op=='=';
                return b.type[0] || numK(v->kind)==skind(r);
            }
            if(kw==4 || v->locked || !b.op) return 0;
            if(v->boxed) return !b.type[0];
            if(b.type[0] && kindFromName(b.type)!=v->kind) return 0;
            return 1;
        }
        // be ke variabel lain: v hanya boleh ada di nilai kanan yang dihitung kode C
        char *rhs=beRhs(t,kw);
        if(!rhs || mentionsUpTo(t,rhs,v->name)) return 0;
        return cgParse(rhs,'s')!=NULL;
    }
    case S_PRINT: case S_THROW: return cgParse(t+5,'s')!=NULL;
    case S_RETURN: return cgParse(t+6,'s')!=NULL;
    case S_CALL: return cgParse(t,'e')!=NULL;
    case S_IF: return cgParse(t+2,'e')!=NULL;
    case S_ELSE: return isElseIf(t) && cgParse(t+7,'e')!=NULL;
    case S_LOOP: return cgParse(t+4,'e')!=NULL;
    case S_OPTIONS: return cgParse(t+7,'e')!=NULL;
    case S_OPTION: return optionList(t)!=NULL;
    case S_FOR: {
        char var[64]="";
        Node *it=forRange(t,var,sizeof var);
        if(strcmp(var,v->name)){
            // v hanya di bagian "in ..."
            char *p=skipWs(t+3);
            if(!idStart(*p)) return 0;
            p=skipWs(readIdent(p,var,sizeof var));
            return strcmp(var,v->name) && startsWord(p,"in") && cgParse(p+2,'e')!=NULL;
        }
        // for v in range(...): semua batasnya int, jadi isinya selalu int
        if(!it || v->locked || numK(v->kind)==K_BOOL) return 0;
        for(int i=0;i<it->n;i++) if(skind(it->kids[i])!=K_INT) return 0;
        return 1;
    }
    }
    return 0;
}

// baris pc ada di dalam for v in range(...) (atau baris for itu sendiri)?
static int insideForOf(int pc, NVar *v){
    for(int h=pc; h>=0; h--){
        if(lineOwner[h]!=lineOwner[pc] || !startsWord(lines[h],"for")) continue;
        char var[64]="";
        if(forRange(lines[h],var,sizeof var) && !strcmp(var,v->name) && pc<blockEnd(h)) return 1;
    }
    return 0;
}

static int nvValid(NVar *v){
    for(int pc=0;pc<lineCount;pc++){
        if(!mentions(lines[pc],v->name)) continue;
        int own=lineOwner[pc];
        if(own>=0 && own!=nvOwner){
            // fungsi lain: di sana nama itu variabelnya sendiri kalau ini lokal; untuk global
            // hanya aman kalau di fungsi itu namanya parameter
            if(nvOwner>=0) continue;
            Func *g=funcAt(own);
            int isParam=0;
            for(int i=0;g && i<g->nparams;i++) if(!strcmp(g->params[i],v->name)) isParam=1;
            if(isParam) continue;
            return 0;
        }
        if(own!=nvOwner) return 0;
        if(nvOwner<0 && lineFile[pc]!=lineFile[v->declPc]) return 0;
        if(pc==nvOwner){
            // baris func: nama parameter dan nilai bawaannya
            if(v->param) continue;
            return 0;
        }
        if(!v->param && pc<v->declPc) return 0;
        if(v->forDecl && !insideForOf(pc,v)) return 0;
        if(!lineOk(pc,v)) return 0;
    }
    return 1;
}

static NVar *nvAdd(const char *name, int kind, int param, int declPc, int locked, int forDecl){
    if(nvCount>=256) return NULL;
    for(int i=0;i<nvCount;i++) if(!strcmp(nv[i].name,name)) return NULL;
    NVar *v=&nv[nvCount++];
    v->boxed=0;
    v->elem=K_ANY;
    snprintf(v->name,sizeof v->name,"%s",name);
    v->kind=kind;
    v->param=param;
    v->declPc=declPc;
    v->locked=locked;
    v->forDecl=forDecl;
    v->alive=1;
    return v;
}

// cari variabel C untuk fungsi f (NULL = program utama)
static void nvAnalyze(Func *f){
    nvCount=0;
    nvVolatile=0;
    nvOwner=f ? f->pc : -1;
    int start=f ? f->pc+1 : 0, stop=f ? blockEnd(f->pc) : lineCount;
    int bodyIndent=f && start<stop ? indents[start] : 0;
    if(f){
        for(int i=0;i<f->nparams;i++){
            int k=f->pkind[i], slot=i+1+(f->ownerType>=0);
            if(nativeKind(k)) nvAdd(f->params[i],k,slot,f->pc,0,0);
            else if(k==K_STRING || k==K_LIST || k==K_MAP || k==K_ARRAY){
                NVar *v=nvAdd(f->params[i],k,slot,f->pc,0,0);
                if(v) v->boxed=1;
            }
        }
    }
    for(int pc=start;pc<stop;pc++){
        if(lineOwner[pc]!=nvOwner) continue;
        curPc=pc;
        if(!stmtKind[pc]) stmtKind[pc]=classify(pc);
        int k=stmtKind[pc];
        if(k==S_TRY) nvVolatile=1;
        if((k==S_BE || k==S_LOCK) && indents[pc]==bodyIndent){
            BeParts b;
            if(!beParts(lines[pc],k==S_BE ? 2 : 4,&b) || b.acc || (b.op && b.op!='=')) continue;
            if(k==S_LOCK && !b.op) continue;
            int kind=0;
            if(b.type[0]) kind=kindFromName(b.type);
            else if(b.op){
                Node *r=cgParse(b.rhs,'s');
                if(r) kind=skind(r);
            }
            if(nativeKind(kind)) nvAdd(b.name,kind,0,pc,k==S_LOCK,0);
            else if(k==S_BE && b.op=='=' && (!b.type[0] || kind==K_STRING || kind==K_LIST || kind==K_MAP || kind==K_ARRAY)){
                // teks/list/map/array: jenisnya diambil saat jalan, sama dengan be biasa.
                // be a = array(...) pasti array (jenis variabel tidak bisa berubah)
                if(!b.type[0]){
                    Node *r=cgParse(b.rhs,'s');
                    if(r && r->kind==N_CALL && !strcmp(r->name,"array") && !userFuncFor("array")) kind=K_ARRAY;
                }
                NVar *v=nvAdd(b.name,b.type[0] || kind==K_ARRAY ? kind : K_ANY,0,pc,0,0);
                if(v){
                    v->boxed=1;
                    v->elem=b.elem[0] ? kindFromName(b.elem) : K_ANY;
                }
            }
        } else if(k==S_FOR){
            char var[64]="";
            if(forRange(lines[pc],var,sizeof var)) nvAdd(var,K_INT,0,pc,0,1);
        }
    }
    // setelah longjmp (try) nilai Value di variabel C tidak bisa dipercaya
    if(nvVolatile) for(int i=0;i<nvCount;i++) if(nv[i].boxed) nv[i].alive=0;
    // buang yang tidak aman sampai tidak ada yang berubah (jenis ekspresi bisa ikut berubah)
    for(int changed=1; changed; ){
        changed=0;
        for(int i=0;i<nvCount;i++){
            if(nv[i].alive && !nvValid(&nv[i])){
                nv[i].alive=0;
                changed=1;
            }
        }
    }
}

// deklarasi variabel C di awal fungsi C
static int cgNF;
static void nvDeclare(void){
    for(int i=0;i<nvCount;i++){
        NVar *v=&nv[i];
        if(!v->alive || (cgNF>=0 && v->param)) continue;
        if(v->boxed){
            if(v->param) fprintf(cg,"RtVar vv_%s={rt_pval(%d),%d,0,NULL};\n",v->name,v->param-1,v->kind);
            else fprintf(cg,"RtVar vv_%s={{0},%d,%d,NULL};\n",v->name,v->kind,v->elem);
            continue;
        }
        fprintf(cg,"%sdouble v_%s=",nvVolatile ? "volatile " : "",v->name);
        if(v->param) fprintf(cg,"rt_param(%d);\n",v->param-1);
        else fputs("0;\n",cg);
    }
}

// keluar dari fungsi: bebaskan nilai RtVar
static void nvFree(void){
    if(nvOwner<0) return;
    for(int i=0;i<nvCount;i++) if(nv[i].alive && nv[i].boxed) fprintf(cg,"valFree(&vv_%s.val); ",nv[i].name);
}

// per baris: milik program utama, isi type, atau fungsi mana
static void nvOwners(void){
    lineOwner=malloc(sizeof(int)*(lineCount+1));
    for(int pc=0;pc<lineCount;pc++) lineOwner[pc]=-1;
    for(int pc=0;pc<lineCount;pc++){
        char *t=lines[pc];
        if(startsWord(t,"ex")) t=skipWs(t+2);
        if(!startsWord(t,"type")) continue;
        int end=blockEnd(pc);
        for(int j=pc;j<end;j++) lineOwner[j]=-2;
    }
    for(int i=0;i<funcCount;i++){
        int end=blockEnd(funcs[i].pc);
        for(int j=funcs[i].pc;j<end;j++) lineOwner[j]=funcs[i].pc;
    }
}

// be/lock ke variabel C: dihitung langsung kalau jenisnya pasti cocok, selain itu lewat Value
// dan dicek rt_unbox (pesan errornya sama dengan be biasa)
static void cgNativeBe(NVar *v, BeParts *b){
    if(v->boxed){
        Node *r=cgParse(b->rhs,'s');
        fputs("{ Value _r=",cg);
        cgExpr(r);
        fprintf(cg,"; rt_vset(&vv_%s,",v->name);
        if(b->op=='=') fputs("_r,",cg);
        else fprintf(cg,"rt_bin(valCopy(vv_%s.val),%d,_r),",v->name,b->op);
        cgStr(v->name);
        fputs("); }\n",cg);
        return;
    }
    if(!b->op){
        fprintf(cg,"v_%s=0;\n",v->name);
        return;
    }
    Node *r=cgParse(b->rhs,'s');
    int rk=skind(r), vk=numK(v->kind);
    if(b->op=='='){
        fprintf(cg,"v_%s=",v->name);
        if(fitsK(vk,rk)){
            cgNum(r);
        } else {
            fputs("rt_unbox(",cg);
            cgExpr(r);
            fprintf(cg,",%d,",v->kind);
            cgStr(v->name);
            fputc(')',cg);
        }
        fputs(";\n",cg);
        return;
    }
    int res=0;
    if(isNumK(vk) && isNumK(rk)) res=b->op=='/' ? K_DOUBLE : vk==K_DOUBLE || rk==K_DOUBLE ? K_DOUBLE : K_INT;
    if(res && fitsK(vk,res)){
        fprintf(cg,"v_%s=({ double _l=v_%s; double _r=",v->name,v->name);
        cgNum(r);
        fputs("; ",cg);
        cgOpTail(b->op);
        fputs(" });\n",cg);
        return;
    }
    // sama dengan execBe: nilai kanan dulu, lalu binop dengan nilai lama
    fprintf(cg,"v_%s=({ Value _r=",v->name);
    cgExpr(r);
    fprintf(cg,"; rt_numop(v_%s,%d,%d,_r,%d,",v->name,vk,b->op,v->kind);
    cgStr(v->name);
    fputs("); });\n",cg);
}

// ---------------- fungsi C asli ----------------
// Fungsi yang semua parameternya dan hasilnya int/double/bool, dan isinya hanya memakai
// variabel C (tidak ada yang dicari lewat nama), ditulis jadi fungsi C biasa:
//     func fib(n int) int   ->   static double nf12(double v_n)
// Dipanggil langsung tanpa frame dan tanpa Value. Yang tetap sama dengan callFuncOwn:
// argumen dihitung dulu, batas rekursi (rt_enter), curPc kembali ke baris pemanggil, dan
// error "tidak ada return". Jenis argumen dan hasil sudah pasti cocok saat build; kalau
// tidak pasti, panggilannya lewat jalan biasa (rt_callf) supaya errornya sama.

static char nfOk[128];   // per fungsi (index di funcs): boleh jadi fungsi C asli
static int cgNF=-1;      // baris func yang sedang ditulis sebagai fungsi C asli, -1 = bukan

// tidak ada variabel yang dicari lewat nama di ekspresi ini
static int noLookup(Node *x){
    if(x->kind==N_VAR) return nvFind(x->name)!=NULL;
    for(int i=0;i<x->n;i++) if(!noLookup(x->kids[i])) return 0;
    return 1;
}

static int exprNL(const char *src, char mode){
    Node *x=cgParse(src,mode);
    return x && noLookup(x);
}

// baris pc ada di dalam loop/for milik fungsi fpc
static int inLoop(int pc, int fpc){
    for(int h=pc-1; h>fpc; h--){
        int k=stmtKind[h];
        if((k==S_LOOP || k==S_FOR) && pc<blockEnd(h)) return 1;
    }
    return 0;
}

// argumen berjenis a boleh masuk parameter berjenis p tanpa error (sama dengan fitKind)
static int argFits(int p, int a){
    p=numK(p);
    if(p==K_DOUBLE) return isNumK(a);
    return a && p==a;
}

static int nfCheck(Func *f){
    if(f->ownerType>=0 || f->nrequired!=f->nparams) return 0;
    int rk=numK(f->rkind);
    if(!isNumK(rk) && rk!=K_BOOL) return 0;
    nvAnalyze(f);
    if(nvVolatile) return 0;
    for(int i=0;i<f->nparams;i++) if(!nvFind(f->params[i])) return 0;
    int stop=blockEnd(f->pc);
    for(int pc=f->pc+1;pc<stop;pc++){
        char *t=lines[pc];
        curPc=pc;
        if(!stmtKind[pc]) stmtKind[pc]=classify(pc);
        switch(stmtKind[pc]){
        case S_BE: case S_LOCK: {
            BeParts b;
            if(!beParts(t,stmtKind[pc]==S_BE ? 2 : 4,&b) || b.acc || !nvFind(b.name)) return 0;
            if(b.rhs && !exprNL(b.rhs,'s')) return 0;
            break;
        }
        case S_IF: if(!exprNL(t+2,'e')) return 0; break;
        case S_ELSE:
            if(isElseIf(t) ? !exprNL(t+7,'e') : !isElse(t)) return 0;
            break;
        case S_LOOP: if(!exprNL(t+4,'e')) return 0; break;
        case S_FOR: {
            char var[64]="";
            Node *rg=forRange(t,var,sizeof var);
            if(!rg || !nvFind(var) || !noLookup(rg)) return 0;
            break;
        }
        case S_RETURN: {
            Node *x=cgParse(t+6,'s');
            if(!x || !noLookup(x) || !fitsK(f->rkind,skind(x))) return 0;
            break;
        }
        case S_BREAK: case S_CONTINUE: if(!inLoop(pc,f->pc)) return 0; break;
        case S_PRINT: case S_THROW: if(!exprNL(t+5,'s')) return 0; break;
        case S_CALL: if(!exprNL(t,'e')) return 0; break;
        case S_OPTIONS: if(!exprNL(t+7,'e')) return 0; break;
        case S_OPTION: {
            Node *x=optionList(t);
            if(!x || !noLookup(x)) return 0;
            break;
        }
        default: return 0;
        }
    }
    return 1;
}

// pemanggilan ini bisa langsung ke fungsi C asli? (jumlah dan jenis argumen pasti cocok)
static Func *nfCallee(Node *x){
    if(x->kind!=N_CALL) return NULL;
    Func *f=userFuncFor(x->name);
    if(!f || !nfOk[f-funcs] || x->n!=f->nparams) return NULL;
    for(int i=0;i<x->n;i++) if(!argFits(f->pkind[i],skind(x->kids[i]))) return NULL;
    return f;
}

static void cgNfCall(Func *f, Node *x){
    fputs("({ ",cg);
    for(int i=0;i<x->n;i++){
        fprintf(cg,"double _p%d=",i);
        cgNum(x->kids[i]);
        fputs("; ",cg);
    }
    fprintf(cg,"int _sp=curPc; rt_enter(); double _r=nf%d(",f->pc);
    for(int i=0;i<x->n;i++) fprintf(cg,"%s_p%d",i ? "," : "",i);
    fprintf(cg,"); rt_leave(); curPc=_sp; if(nrx_noret){ nrx_noret=0; rt_noreturn(%d); } _r; })",f->pc);
}

// static double nf12(double v_a, double v_b)
static void cgNfHead(Func *f){
    fprintf(cg,"static double nf%d(",f->pc);
    for(int i=0;i<f->nparams;i++) fprintf(cg,"%sdouble v_%s",i ? ", " : "",f->params[i]);
    if(!f->nparams) fputs("void",cg);
    fputc(')',cg);
}

// "Value _a[n]; _a[0]=...; ..." untuk isi node mulai dari kids[from];
// asText: tiap bagian langsung dijadikan teks (teks berjejer, lihat rt_tostr)
static int cgArgs(Node *x, int from, int asText){
    int n=x->n-from;
    if(n<=0){
        fputs("Value *_a=NULL; ",cg);
        return 0;
    }
    fprintf(cg,"Value _a[%d]; ",n);
    for(int i=0;i<n;i++){
        fprintf(cg,"_a[%d]=%s",i,asText ? "rt_tostr(" : "");
        cgExpr(x->kids[from+i]);
        fputs(asText ? "); " : "; ",cg);
    }
    return n;
}

// dua sisi dihitung kiri dulu baru kanan (urutan argumen fungsi C tidak pasti)
static void cgPair(Node *x, const char *fn, int op){
    fputs("({ Value _l=",cg);
    cgExpr(x->kids[0]);
    fputs("; Value _r=",cg);
    cgExpr(x->kids[1]);
    fprintf(cg,"; %s(_l,%d,_r); })",fn,op);
}

static void cgExpr(Node *x){
    // hitungan dengan variabel C: langsung double, baru jadi Value di akhir
    if((x->kind==N_BIN || x->kind==N_NEG || x->kind==N_CMP || x->kind==N_NOT || x->kind==N_AND || x->kind==N_OR)
       && hasNative(x) && nativeOk(x)){
        cgBoxOpen(skind(x));
        cgNum(x);
        fputc(')',cg);
        return;
    }
    switch(x->kind){
    case N_LIT:
        if(x->lit.type==T_NUM) fprintf(cg,"%s(%.17g)",x->lit.sub ? "valDec" : "valNum",x->lit.num);
        else if(x->lit.sub) fprintf(cg,"valBool(%d)",x->lit.str[0]=='t');
        else {
            fputs("valStr(",cg);
            cgStr(x->lit.str);
            fputc(')',cg);
        }
        break;
    case N_VAR: {
        NVar *v=nvAny(x->name);
        if(v && v->boxed){
            fprintf(cg,"valCopy(vv_%s.val)",v->name);
            break;
        }
        if(v){
            cgBoxOpen(numK(v->kind));
            fprintf(cg,"v_%s)",v->name);
            break;
        }
        fputs("rt_var(",cg);
        cgStr(x->name);
        fputc(')',cg);
        break;
    }
    case N_LIST: case N_MAP: case N_CONCAT: {
        fputs("({ ",cg);
        int n=cgArgs(x,0,x->kind==N_CONCAT);
        fprintf(cg,"%s(_a,%d); })",x->kind==N_LIST ? "rt_list" : x->kind==N_MAP ? "rt_map" : "rt_concat",n);
        break;
    }
    case N_CALL: {
        Func *nf=nfCallee(x);
        if(nf){
            cgBoxOpen(numK(nf->rkind));
            cgNfCall(nf,x);
            fputc(')',cg);
            break;
        }
        fputs("({ ",cg);
        int n=cgArgs(x,0,0);
        // fungsi buatan sendiri: langsung, tanpa mencocokkan nama dengan fungsi bawaan dulu
        Func *f=userFuncFor(x->name);
        if(f){
            fprintf(cg,"rt_callf(%d,_a,%d); })",f->pc,n);
            break;
        }
        // fungsi bawaan: nomornya dicari sekarang, saat jalan tidak ada pencarian nama
        // (ffi dengan nama sama tetap didahulukan di callNamedId)
        fprintf(cg,"rt_callb(%d,",builtinId(x->name));
        cgStr(x->name);
        fprintf(cg,",_a,%d); })",n);
        break;
    }
    case N_METHOD: {
        fputs("({ Value _o=",cg);
        cgExpr(x->kids[0]);
        fputs("; rt_method_check(&_o,",cg);
        cgStr(x->name);
        fputs("); ",cg);
        int n=cgArgs(x,1,0);
        fputs("rt_method(_o,",cg);
        cgStr(x->name);
        fprintf(cg,",_a,%d); })",n);
        break;
    }
    case N_INDEX: {
        NVar *bv=x->kids[0]->kind==N_VAR ? nvAny(x->kids[0]->name) : NULL;
        if(bv && bv->boxed){
            fprintf(cg,"rt_index_at(&vv_%s.val,",bv->name);
            cgExpr(x->kids[1]);
            fputc(')',cg);
            break;
        }
        fputs("({ Value _b=",cg);
        cgExpr(x->kids[0]);
        fputs("; Value _i=",cg);
        cgExpr(x->kids[1]);
        fputs("; rt_index(_b,_i); })",cg);
        break;
    }
    case N_FIELD:
        fputs("rt_field(",cg);
        cgExpr(x->kids[0]);
        fputc(',',cg);
        cgStr(x->name);
        fputc(')',cg);
        break;
    case N_NEG:
        fputs("rt_neg(",cg);
        cgExpr(x->kids[0]);
        fputc(')',cg);
        break;
    case N_BIN: cgPair(x,"rt_bin",x->op); break;
    case N_CMP: cgPair(x,"rt_cmp",x->op); break;
    case N_IN: cgPair(x,"rt_in",x->op); break;
    case N_NOT:
        fputs("valBool(!rt_truthy(",cg);
        cgExpr(x->kids[0]);
        fputs("))",cg);
        break;
    case N_AND: case N_OR:
        fputs("valBool(rt_truthy(",cg);
        cgExpr(x->kids[0]);
        fputs(x->kind==N_AND ? ") && rt_truthy(" : ") || rt_truthy(",cg);
        cgExpr(x->kids[1]);
        fputs("))",cg);
        break;
    }
}

// baca teks jadi pohon; kalau salah tulis, NULL (errornya baru muncul saat jalan)
static Node *cgParse(const char *src, char mode){
    Node *x=NULL;
    if(RT_SETJMP(tryBufs[tryTop++])==0){
        x=astFor(src,mode);
        tryTop--;
    } else {
        tryTop--;
        x=NULL;
    }
    return x;
}

// mode 'e' = satu ekspresi, 's' = beberapa ekspresi berjejer
static void cgEval(const char *src, char mode){
    Node *x=cgParse(src,mode);
    if(x){
        cgExpr(x);
        return;
    }
    fputs("rt_eval(",cg);
    cgStr(src);
    fprintf(cg,",%d)",mode=='s');
}

// syarat if/loop: cgCond, atau dihitung saat jalan kalau salah tulis
static void cgCondEval(const char *src){
    Node *x=cgParse(src,'e');
    if(x){
        cgCond(x);
        return;
    }
    fputs("rt_truthy(rt_eval(",cg);
    cgStr(src);
    fputs(",0))",cg);
}

// nilai kanan "be nama ... = nilai" / "+= nilai"; NULL kalau dihitung execBe sendiri
// (ask, [] dan {}, atau baris yang salah tulis)
static char *beRhs(char *text, int kwLen){
    char *p=skipWs(text+kwLen);
    char word[64];
    if(!idStart(*p)) return NULL;
    p=readIdent(p,word,sizeof word);
    while(*p=='[' || (*p=='.' && idStart(p[1]))){
        if(*p=='['){
            int d=0;
            char *q=p;
            for(; *q; q++){
                if(*q=='[') d++;
                else if(*q==']' && --d==0) break;
            }
            if(!*q) return NULL;
            p=q+1;
        } else {
            p=readIdent(p+1,word,sizeof word);
        }
    }
    p=skipWs(p);
    if(idStart(*p)){
        p=skipWs(readIdent(p,word,sizeof word));
        if(idStart(*p)) p=skipWs(readIdent(p,word,sizeof word));
    }
    if(*p=='=' && p[1]!='=') p++;
    else if(*p && strchr("+-*/%",*p) && p[1]=='=') p+=2;
    else return NULL;
    p=skipWs(p);
    if(!*p || startsWord(p,"ask") || !strcmp(p,"[]") || !strcmp(p,"{}")) return NULL;
    return p;
}

// keadaan saat menulis satu blok
typedef struct {
    int tries;       // try yang sedang aktif di fungsi ini
    int loopTries;   // nilai tries saat loop terdalam dimulai; -1 = tidak di dalam loop
    int nfor;        // iterator for yang aktif di fungsi ini (dilepas saat return)
    int forIds[64];
} CgCtx;

static void cgRange(int start, int stop, CgCtx c);

// keluar dari fungsi: lepas iterator for dan try yang masih aktif
static void nvFree(void);
static void cgLeave(CgCtx *c){
    nvFree();
    for(int i=0;i<c->nfor;i++) fprintf(cg,"rt_release(_it%d); ",c->forIds[i]);
    if(c->tries) fprintf(cg,"rt_try_pop(%d); ",c->tries);
}

// perintah be/lock: nilai kanannya dihitung kode C, sisanya oleh rt_stmt
static void cgStmtBe(int pc, char *text, int kwLen){
    AccBe a;
    if(accBe(text,kwLen,&a)){
        NVar *av=nvAny(a.name);
        Node *rv=cgParse(a.rhs,'s');
        int ik=skind(a.idx[0]), rk=skind(rv);
        if(a.n==1 && av && av->boxed && av->kind==K_ARRAY && strchr("=+-*",a.op)
           && (ik==K_INT || ik==K_DOUBLE) && (rk==K_INT || rk==K_DOUBLE)){
            // array dengan index dan nilai angka: langsung double, urutannya sama dengan execBe
            fputs("{ double _v=",cg);
            cgNum(rv);
            fprintf(cg,"; rt_lockchk(&vv_%s.val,",av->name);
            cgStr(a.name);
            fprintf(cg,"); rt_arr_set(&vv_%s.val,",av->name);
            cgNum(a.idx[0]);
            fprintf(cg,",%d,_v); }\n",a.op);
            return;
        }
        // urutan sama dengan execBe: nilai kanan, variabel, lalu tiap index setelah cek lock
        NVar *r=nvAny(a.name);
        fputs("{ Value _v=",cg);
        cgEval(a.rhs,'s');
        if(r && r->boxed) fprintf(cg,"; Value *_t=&vv_%s.val;",r->name);
        else {
            fputs("; Value *_t=rt_root(",cg);
            cgStr(a.name);
            fputs(");",cg);
        }
        for(int i=0;i<a.n;i++){
            fputs(" rt_lockchk(_t,",cg);
            cgStr(a.name);
            fputs("); _t=rt_step(_t,",cg);
            cgExpr(a.idx[i]);
            fprintf(cg,",%d,",i==a.n-1);
            cgStr(a.name);
            fputs(");",cg);
        }
        fprintf(cg," rt_put(_t,%d,_v,",a.op);
        cgStr(a.name);
        fputs("); }\n",cg);
        return;
    }
    BeParts b;
    NVar *v;
    if(beParts(text,kwLen,&b) && !b.acc && (v=nvAny(b.name))){
        cgNativeBe(v,&b);
        return;
    }
    char *rhs=beRhs(text,kwLen);
    if(!rhs){
        fprintf(cg,"rt_stmt(%d,NULL);\n",pc);
        return;
    }
    fputs("{ Value _v=",cg);
    cgEval(rhs,'s');
    fprintf(cg,"; rt_stmt(%d,&_v); }\n",pc);
}

static void cgIf(int pc, int stop, CgCtx c){
    // pc: baris if atau else if; cabang berikutnya ditulis di dalam else { }
    char *text=lines[pc];
    fprintf(cg,"curPc=%d;\nif(",pc);
    cgCondEval(startsWord(text,"if") ? text+2 : text+7);
    fputs("){\n",cg);
    cgRange(pc+1,blockEnd(pc),c);
    fputs("}\n",cg);
    int next=nextBranch(pc,stop);
    if(next<0) return;
    fputs("else {\n",cg);
    if(isElse(lines[next])){
        fprintf(cg,"curPc=%d;\n",next);
        cgRange(next+1,blockEnd(next),c);
    } else {
        cgIf(next,stop,c);
    }
    fputs("}\n",cg);
}

static void cgOptions(int pc, int end, CgCtx c){
    int id=cgId++;
    int childIndent=indents[pc+1];
    int elseIdx=-1, k=0;
    int bodies[256], nb=0;
    fprintf(cg,"{ Value _ov%d=",id);
    cgEval(lines[pc]+7,'e');
    fprintf(cg,"; int _c%d=-1;\n",id);
    for(int j=pc+1; j<end; j=blockEnd(j)){
        fprintf(cg,"curPc=%d;\n",j);
        if(indents[j]!=childIndent){
            fprintf(cg,"rt_fail(%d,RT_FAIL_OPT_INDENT);\n",j);
            break;
        }
        if(nb<256) bodies[nb++]=j;
        k=nb-1;
        if(isElse(lines[j])){
            elseIdx=k;
            continue;
        }
        if(!startsWord(lines[j],"option")){
            fprintf(cg,"rt_fail(%d,RT_FAIL_OPT_ITEM);\n",j);
            break;
        }
        char *src=lines[j]+6;
        char *buf=malloc(strlen(src)+3);
        sprintf(buf,"[%s]",src);
        fprintf(cg,"if(_c%d<0 && rt_opt_match(&_ov%d,",id,id);
        cgEval(buf,'e');
        fprintf(cg,")) _c%d=%d;\n",id,k);
        free(buf);
    }
    fprintf(cg,"valFree(&_ov%d);\nif(_c%d<0) _c%d=%d;\n",id,id,id,elseIdx);
    for(int i=0;i<nb;i++){
        fprintf(cg,"%sif(_c%d==%d){\n",i ? "else " : "",id,i);
        cgRange(bodies[i]+1,blockEnd(bodies[i]),c);
        fputs("}\n",cg);
    }
    fputs("}\n",cg);
}

static void cgRange(int start, int stop, CgCtx c){
    for(int pc=start; pc<stop; ){
        char *text=lines[pc];
        if(!stmtKind[pc]) stmtKind[pc]=classify(pc);
        int kind=stmtKind[pc];
        if(kind==S_SKIP || kind==S_LIB){
            pc=kind==S_SKIP ? blockEnd(pc) : pc+1;
            continue;
        }
        if(kind==S_FFI){
            pc=ffiEnd(pc)+1;
            continue;
        }
        if(kind==S_WINDOW && windowLib && nrxWindowIsTemplate(pc)){
            pc=nrxWindowNext(pc);
            continue;
        }
        fprintf(cg,"curPc=%d;\n",pc);
        curPc=pc;   // file baris ini, untuk mencari fungsi yang dipanggil
        switch(kind){
        case S_BE:
            cgStmtBe(pc,text,2);
            pc++;
            break;
        case S_LOCK:
            cgStmtBe(pc,text,4);
            pc++;
            break;
        case S_EX: {
            char *rest=skipWs(text+2);
            if(startsWord(rest,"be")) cgStmtBe(pc,text,(int)(rest-text)+2);
            else if(startsWord(rest,"lock")) cgStmtBe(pc,text,(int)(rest-text)+4);
            else fprintf(cg,"rt_stmt(%d,NULL);\n",pc);
            pc=startsWord(rest,"func") || startsWord(rest,"type") ? blockEnd(pc) : pc+1;
            break;
        }
        case S_DELETE: case S_ASK: case S_WAIT: case S_ERRLANG:
            fprintf(cg,"rt_stmt(%d,NULL);\n",pc);
            pc++;
            break;
        case S_PRINT:
            fputs("rt_print(",cg);
            cgEval(text+5,'s');
            fputs(");\n",cg);
            pc++;
            break;
        case S_CALL:
            fputs("{ Value _v=",cg);
            cgEval(text,'e');
            fputs("; valFree(&_v); }\n",cg);
            pc++;
            break;
        case S_THROW:
            fputs("rt_throw(",cg);
            cgEval(text+5,'s');
            fputs(");\n",cg);
            pc++;
            break;
        case S_RETURN:
            if(cgNF>=0){
                fputs("{ double _rv=",cg);
                cgNum(cgParse(text+6,'s'));
                fputs("; return _rv; }\n",cg);
                pc++;
                break;
            }
            fputs("rt_return_check(); rt_return(",cg);
            cgEval(text+6,'s');
            fputs("); ",cg);
            cgLeave(&c);
            fprintf(cg,"return %d;\n",RC_RETURN);
            pc++;
            break;
        case S_BREAK: case S_CONTINUE:
            if(c.loopTries>=0){
                if(c.tries>c.loopTries) fprintf(cg,"rt_try_pop(%d); ",c.tries-c.loopTries);
                fputs(kind==S_BREAK ? "break;\n" : "continue;\n",cg);
            } else {
                // tidak ada loop di fungsi ini: naik ke pemanggil, seperti interpreter
                cgLeave(&c);
                fprintf(cg,"return %d;\n",kind==S_BREAK ? RC_BREAK : RC_CONTINUE);
            }
            pc++;
            break;
        case S_IF: {
            cgIf(pc,stop,c);
            int last=pc;
            for(int n; (n=nextBranch(last,stop))>=0; ) last=n;
            pc=blockEnd(last);
            break;
        }
        case S_LOOP: {
            int end=blockEnd(pc);
            CgCtx in=c;
            in.loopTries=c.tries;
            fprintf(cg,"for(;;){\ncurPc=%d;\nif(!",pc);
            cgCondEval(text+4);
            fputs(") break;\n",cg);
            cgRange(pc+1,end,in);
            fputs("}\n",cg);
            pc=end;
            break;
        }
        case S_FOR: {
            int end=blockEnd(pc);
            char var[64];
            char *p=skipWs(text+3);
            if(!idStart(*p)){
                fprintf(cg,"rt_fail(%d,RT_FAIL_FOR);\n",pc);
                pc=end;
                break;
            }
            p=skipWs(readIdent(p,var,sizeof var));
            if(!startsWord(p,"in")){
                fprintf(cg,"rt_fail(%d,RT_FAIL_FOR);\n",pc);
                pc=end;
                break;
            }
            int id=cgId++;
            char rv[64];
            Node *rg=forRange(text,rv,sizeof rv);
            if(rg){
                // for i in range(...): dihitung langsung tanpa membuat list (batas dan errornya sama)
                NVar *v=nvFind(var);
                fprintf(cg,"{ double _f%d,_t%d,_s%d;\n{ ",id,id,id);
                int n=cgArgs(rg,0,0);
                fprintf(cg,"rt_range(_a,%d,&_f%d,&_t%d,&_s%d); }\n",n,id,id,id);
                fprintf(cg,"for(double _x%d=_f%d; _s%d>0 ? _x%d<_t%d : _x%d>_t%d; _x%d+=_s%d){\ncurPc=%d;\n",
                        id,id,id,id,id,id,id,id,id,pc);
                if(v) fprintf(cg,"v_%s=_x%d;\n",var,id);
                else {
                    fputs("rt_setloopvar(",cg);
                    cgStr(var);
                    fprintf(cg,",valNum(_x%d));\n",id);
                }
                CgCtx in=c;
                in.loopTries=c.tries;
                cgRange(pc+1,end,in);
                fputs("}\n}\n",cg);
                pc=end;
                break;
            }
            fprintf(cg,"{ List *_it%d=rt_iter(",id);
            cgEval(p+2,'e');
            fprintf(cg,");\nfor(int _i%d=0; _i%d<_it%d->count; _i%d++){\ncurPc=%d;\nrt_setloopvar(",id,id,id,id,pc);
            cgStr(var);
            fprintf(cg,",valCopy(_it%d->items[_i%d]));\n",id,id);
            CgCtx in=c;
            in.loopTries=c.tries;
            if(in.nfor<64) in.forIds[in.nfor++]=id;
            cgRange(pc+1,end,in);
            fprintf(cg,"}\nrt_release(_it%d); }\n",id);
            pc=end;
            break;
        }
        case S_TRY: {
            int catchLine=blockEnd(pc);
            if(catchLine>=lineCount || indents[catchLine]!=indents[pc] || !startsWord(lines[catchLine],"catch")){
                fprintf(cg,"rt_fail(%d,RT_FAIL_TRY);\n",pc);
                pc=catchLine;
                break;
            }
            int catchEnd=blockEnd(catchLine);
            int id=cgId++;
            CgCtx in=c;
            in.tries++;
            fprintf(cg,"{ int _d%d=rt_depth();\nif(RT_SETJMP(*rt_try_push())==0){\n",id);
            cgRange(pc+1,catchLine,in);
            fprintf(cg,"rt_try_pop(1);\n} else {\nrt_catch(%d,_d%d);\n",catchLine,id);
            cgRange(catchLine+1,catchEnd,c);
            fputs("} }\n",cg);
            pc=catchEnd;
            break;
        }
        case S_OPTIONS: {
            int end=blockEnd(pc);
            if(end>pc+1) cgOptions(pc,end,c);
            pc=end;
            break;
        }
        case S_CATCH:
            fprintf(cg,"rt_fail(%d,RT_FAIL_CATCH);\n",pc);
            pc=blockEnd(pc);
            break;
        case S_OPTION:
            fprintf(cg,"rt_fail(%d,RT_FAIL_OPTION);\n",pc);
            pc=blockEnd(pc);
            break;
        case S_ELSE:
            // else tanpa if di atasnya
            fprintf(cg,"rt_unknown(%d);\n",pc);
            pc=blockEnd(pc);
            break;
        case S_WINDOW:
            // closeWindow / newWindow; tanpa lib nrx_window.h errornya juga dari rt_stmt
            fprintf(cg,"rt_stmt(%d,NULL);\n",pc);
            pc++;
            break;
        default:
            fprintf(cg,"rt_unknown(%d);\n",pc);
            pc++;
        }
    }
}

// isi file apa adanya (untuk ditanam di .exe)
static char *readWhole(const char *path){
    FILE *f=fopen(path,"rb");
    if(!f) return strdup("");
    fseek(f,0,SEEK_END);
    long n=ftell(f);
    fseek(f,0,SEEK_SET);
    char *buf=malloc(n+1);
    n=(long)fread(buf,1,n,f);
    buf[n]=0;
    fclose(f);
    // \r\n jadi \n; pembaca baris membuang \r juga, tapi string C jadi lebih pendek
    char *w=buf;
    for(char *r=buf; *r; r++) if(*r!='\r') *w++=*r;
    *w=0;
    return buf;
}

// pesan build (bukan error program, jadi tidak lewat tabel pesan)
static void buildMsg(const char *id, const char *en, const char *arg){
    printf(errLang ? en : id,arg);
    printf("\n");
}

// nrxlang build file.nrxl [-o file.exe]
static int buildMain(int argc, char **argv){
    if(argc<3){
        printf("usage: %s build file.nrxl [-o file.exe]\n",argv[0]);
        return 1;
    }
    char out[1024];
    const char *path=argv[2];
    if(argc>=5 && !strcmp(argv[3],"-o")) snprintf(out,sizeof out,"%s",argv[4]);
    else {
        snprintf(out,sizeof out,"%s",path);
        char *dot=strrchr(out,'.');
        if(dot && !strcmp(dot,".nrxl")) *dot=0;
        strncat(out,".exe",sizeof out-strlen(out)-1);
    }

    srcFile=(char*)path;
    detectErrLang(srcFile);
    // runtime (libnrxrt.a), header, dan gcc dari installer ada di folder nrxlang.exe
    char dir[1024]=".";
    #ifdef _WIN32
    GetModuleFileNameA(NULL,dir,sizeof dir);
    #endif
    char *slash=strrchr(dir,'\\');
    if(!slash) slash=strrchr(dir,'/');
    if(slash) *slash=0;
    // gcc yang ikut installer (dir\gcc\bin) didahulukan, selain itu gcc di PATH
    char gcc[1100]="gcc";
    #ifdef _WIN32
    char own[1100];
    snprintf(own,sizeof own,"%s\\gcc\\bin\\gcc.exe",dir);
    FILE *gf=fopen(own,"rb");
    if(gf){
        fclose(gf);
        snprintf(gcc,sizeof gcc,"\"%s\"",own);
    }
    char probe[1300];
    snprintf(probe,sizeof probe,"\"%s --version >NUL 2>&1\"",gcc);
    int noGcc=system(probe)!=0;
    #else
    int noGcc=system("gcc --version >/dev/null 2>&1")!=0;
    #endif
    if(noGcc){
        buildMsg("error: butuh gcc untuk build (untuk sekarang); pasang gcc, atau jalankan langsung: nrxlang run %s",
                 "error: you need gcc to build this (for now); install gcc, or run it directly: nrxlang run %s",path);
        return 1;
    }

    // dibaca dan dicek sama persis dengan menjalankan langsung; error tipe muncul di sini
    loadFile(srcFile,-1,0);
    checkImNames();
    retVal=valStr("");
    prepass();
    checkProgram();

    char cpath[1100];
    snprintf(cpath,sizeof cpath,"%s",out);
    char *dot=strrchr(cpath,'.');
    if(dot && !strcmp(dot,".exe")) *dot=0;
    strncat(cpath,".c",sizeof cpath-strlen(cpath)-1);
    cg=fopen(cpath,"w");
    if(!cg){
        buildMsg("tidak bisa menulis %s","cannot write %s",cpath);
        return 1;
    }
    fprintf(cg,"// dibuat oleh nrxlang build dari %s; jangan diubah manual\n#include \"nrxrt.h\"\n\n",path);

    fputs("static const char *nrxNames[]={",cg);
    for(int i=0;i<fileCount;i++){
        cgStr(files[i]);
        fputc(',',cg);
    }
    fputs("};\nstatic const char *nrxTexts[]={\n",cg);
    for(int i=0;i<fileCount;i++){
        char *t=readWhole(files[i]);
        cgStr(t);
        fputs(",\n",cg);
        free(t);
    }
    fputs("};\n\n",cg);
    if(threeDLib) fputs("void nrx3dInit(void);\n",cg);

    CgCtx top={0,-1,0,{0}};
    nvOwners();
    fputs("static int nrx_noret;\n",cg);
    for(int i=0;i<funcCount && i<128;i++){
        nfOk[i]=nfCheck(&funcs[i]);
        if(nfOk[i]){
            cgNfHead(&funcs[i]);
            fputs(";\n",cg);
        }
    }
    fputs("\n",cg);
    for(int i=0;i<funcCount;i++){
        Func *f=&funcs[i];
        if(i<128 && nfOk[i]){
            // fungsi C asli + pintu masuk biasa (dipanggil callFuncOwn, mis. dari interpreter)
            fprintf(cg,"// %s (baris %d), fungsi C asli\n",f->name,lineNo[f->pc]);
            nvAnalyze(f);
            cgNF=f->pc;
            cgNfHead(f);
            fputs("{\n",cg);
            nvDeclare();
            cgRange(f->pc+1,blockEnd(f->pc),top);
            fputs("nrx_noret=1;\nreturn 0;\n}\n",cg);
            cgNF=-1;
            fprintf(cg,"static int f%d(void){\ndouble _r=nf%d(",f->pc,f->pc);
            for(int k=0;k<f->nparams;k++) fprintf(cg,"%srt_param(%d)",k ? "," : "",k);
            fputs(");\nif(nrx_noret){ nrx_noret=0; return 0; }\nrt_return(",cg);
            cgBoxOpen(numK(f->rkind));
            fputs("_r));\nreturn 1;\n}\n\n",cg);
            continue;
        }
        fprintf(cg,"// %s (baris %d)\nstatic int f%d(void){\n",f->name,lineNo[f->pc],f->pc);
        nvAnalyze(f);
        nvDeclare();
        cgRange(f->pc+1,blockEnd(f->pc),top);
        nvFree();
        fputs("return 0;\n}\n\n",cg);
    }
    fputs("static int fTop(void){\n",cg);
    nvAnalyze(NULL);
    nvDeclare();
    cgRange(0,lineCount,top);
    fputs("return 0;\n}\n\nint main(void){\n",cg);
    // raylib hanya ikut kalau program memakai lib nrx_3d.h
    if(threeDLib) fputs("nrx3dInit();\n",cg);
    for(int i=0;i<funcCount;i++) fprintf(cg,"rt_body(%d,f%d);\n",funcs[i].pc,funcs[i].pc);
    fputs("return rt_main(",cg);
    cgStr(path);
    fprintf(cg,",nrxNames,nrxTexts,%d,fTop);\n}\n",fileCount);
    fclose(cg);

    char cmd[4096];
    char extra[1200]="";
    if(threeDLib) snprintf(extra,sizeof extra," \"%s\\libraylib.a\" -lopengl32 -lgdi32 -lwinmm",dir);
    snprintf(cmd,sizeof cmd,"%s -O2 -w \"%s\" -I\"%s\\include\" \"%s\\libnrxrt.a\"%s -o \"%s\"",gcc,cpath,dir,dir,extra,out);
    #ifdef _WIN32
    // cmd.exe membuang tanda kutip paling luar kalau perintahnya diawali kutip
    char wrapped[4200];
    snprintf(wrapped,sizeof wrapped,"\"%s\"",cmd);
    int rc=system(wrapped);
    #else
    int rc=system(cmd);
    #endif
    if(rc!=0){
        buildMsg("gcc gagal membuat %s (apakah gcc terpasang dan ada di PATH?)",
                 "gcc failed to build %s (is gcc installed and on PATH?)",out);
        return 1;
    }
    if(!getenv("NRX_KEEP_C")) remove(cpath);
    return 0;
}
