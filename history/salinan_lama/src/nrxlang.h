// nrxlang.h: bagian inti nrxlang yang dipakai bersama oleh nrxlang.c dan library nrx_*.c
// build:  build.bat  (gcc src/nrxlang.c lib/nrx_*.c -Isrc -Ilib/headers -o dist/nrxlang.exe)
#ifndef NRXLANG_H
#define NRXLANG_H

#define MAX_LINES 4096

// nilai di nrxl: teks, angka, list, map, atau array (deretan angka desimal polos)
typedef enum { T_STR, T_NUM, T_LIST, T_MAP, T_ARR } VType;
typedef struct List List;
typedef struct Map Map;
typedef struct NumArr NumArr;
typedef struct {
    VType type;
    double num;
    char *str;
    List *list;
    Map *map;
    int sub;      // T_NUM: 1 = desimal (bukan int). T_STR: 1 = bool (true/false)
    NumArr *arr;
} Value;

// jenis variabel dan isi list/map (tipe ketat). K_ANY = belum diketahui, ditentukan
// oleh nilai pertama. K_MIXED = boleh campur (hasil fromJson)
enum { K_ANY, K_INT, K_DOUBLE, K_FLOAT, K_BOOL, K_STRING, K_LIST, K_MAP, K_STRUCT, K_MIXED, K_ARRAY };

// array: isinya double C biasa (bukan Value), seperti double[] di C. Cepat dan hemat
// memori untuk data angka besar; bisa dikirim ke ffi sebagai pointer (double*)
struct NumArr {
    double *d;
    int count, cap;
    int locked;
    int refs;
};

struct List {
    Value *items;
    int count, cap;
    int locked;   // dikunci lewat lock: isinya tidak boleh diubah
    int elem;     // jenis isi (K_...)
    const char *ename;   // nama type isinya kalau elem == K_STRUCT
    int refs;     // jumlah pemegang; dibebaskan saat jadi 0
};
// map menyimpan key (teks) sesuai urutan dimasukkan
struct Map {
    char **keys;
    Value *vals;
    int count, cap;
    const char *structName;   // nama struct kalau map ini dibuat dari struct, selain itu NULL
    int locked;               // dikunci lewat lock: isinya tidak boleh diubah
    int elem;                 // jenis nilai (K_...); struct tidak memakai ini
    const char *ename;        // nama type nilainya kalau elem == K_STRUCT
    int refs;                 // jumlah pemegang; dibebaskan saat jadi 0
};

// isi file .nrxl yang sedang dijalankan
extern char lines[MAX_LINES][512];
extern int indents[MAX_LINES];
extern int lineNo[MAX_LINES];
extern int lineCount;
extern char *srcFile;

// tampilkan error (dengan nomor baris) lalu program berhenti
void nrxFail(const char *fmt, ...);
// terjemahan satu pesan dari tabel src/nrx_msgs.h (untuk potongan pesan yang disisipkan ke %s)
const char *nrxTr(const char *msg);
void nrxFailAt(int pc, const char *fmt, ...);

Value valNum(double d);      // int kalau d bilangan bulat, selain itu desimal
Value valDec(double d);      // selalu desimal
int valKind(Value v);        // K_INT, K_DOUBLE, K_STRING, ...
Value valStr(const char *s);
Value valStrOwn(char *s);
Value valBool(int b);
Value valList(List *l);
Value valArr(NumArr *a);
NumArr *arrNew(int n);
Value valCopy(Value v);
void valFree(Value *v);
char *valToStr(Value v);
double valToNum(Value v);
int numLike(Value v, double *out);
int valTruthy(Value v);
List *listNew(void);
void listPush(List *l, Value v);
Value valMap(Map *m);
Map *mapNew(void);
Value *mapGet(Map *m, const char *key);
void mapSet(Map *m, const char *key, Value v);

// evalExpr: satu ekspresi; evalSeq: beberapa ekspresi berjejer digabung jadi teks ("skor: " skor)
Value evalExpr(const char *src);
Value evalSeq(const char *src);
char *evalSeqStr(const char *src);

Value *findGlobalVal(const char *name);
void setGlobalVar(const char *name, Value v);

int startsWord(const char *s, const char *w);
void trim(char *s);
int splitArgs(const char *s, char out[][256], int max);
void funcName(const char *s, char *out, int size);
int findFunc(const char *name);
int funcParamCount(int fpc);
Value callFunc(int fpc, Value *args, int nargs);

#endif
