// nrxrt.h: fungsi runtime yang dipanggil kode C hasil "nrxlang build".
// Runtime-nya sama dengan interpreter (dist/libnrxrt.a), jadi hasil, pesan error,
// dan nomor barisnya sama persis dengan menjalankan file .nrxl langsung.
// Semua rt_* yang menerima Value membebaskan nilai itu.
#ifndef NRXRT_H
#define NRXRT_H
#include <setjmp.h>
#include <math.h>
#include "nrxlang.h"

extern int curPc;   // baris yang sedang jalan (untuk pesan error)

// ekspresi
Value rt_var(const char *name);
int rt_truthy(Value v);
Value rt_list(Value *a, int n);
Value rt_map(Value *kv, int n);
Value rt_call(const char *name, Value *a, int n);
Value rt_callb(int id, const char *name, Value *a, int n);
Value rt_index(Value v, Value idx);
Value rt_index_at(Value *v, Value idx);
double rt_arr_get(Value *a, double i);
void rt_arr_set(Value *a, double i, int op, double v);
Value rt_field(Value v, const char *name);
void rt_method_check(Value *o, const char *name);
Value rt_method(Value o, const char *name, Value *a, int n);
Value rt_neg(Value v);
Value rt_bin(Value a, int op, Value b);
Value rt_cmp(Value a, int op, Value b);
Value rt_in(Value a, int negate, Value b);
Value rt_tostr(Value v);
Value rt_concat(Value *a, int n);
Value rt_eval(const char *src, int seq);
Value rt_callf(int fpc, Value *a, int n);

// perintah
void rt_stmt(int pc, Value *pre);
void rt_print(Value v);
void rt_throw(Value v);
void rt_return_check(void);
void rt_return(Value v);
List *rt_iter(Value v);
void rt_setvar(const char *name, Value v);
void rt_setloopvar(const char *name, Value v);
double rt_sqrt(double d);
void rt_release(List *l);

// variabel C: int/double/bool yang disimpan sebagai double (lihat nrx_build.c)
double rt_unbox(Value v, int kind, const char *name);
double rt_numop(double l, int lk, int op, Value r, int kind, const char *name);
double rt_unum(Value v);
double rt_param(int i);
void rt_div0(void);
void rt_enter(void);
void rt_leave(void);
void rt_noreturn(int fpc);
// variabel C jenis lain (teks, list, map): Value dan jenisnya, dicek seperti setVar
typedef struct { Value val; int kind, elem; const char *sname; } RtVar;
Value rt_pval(int i);
Value *rt_root(const char *name);
void rt_lockchk(Value *base, const char *name);
Value *rt_step(Value *base, Value idx, int last, const char *name);
void rt_put(Value *t, int op, Value v, const char *name);
void rt_vset(RtVar *r, Value v, const char *name);
void rt_range(Value *a, int n, double *from, double *to, double *step);
int rt_opt_match(Value *v, Value options);
enum { RT_FAIL_OPT_INDENT, RT_FAIL_OPT_ITEM, RT_FAIL_FOR, RT_FAIL_TRY, RT_FAIL_CATCH, RT_FAIL_OPTION };
void rt_fail(int pc, int which);
void rt_unknown(int pc);

// try/catch. Di Windows 64-bit, setjmp biasa membuat longjmp membongkar stack lewat SEH;
// melewati fungsi C yang dioptimasi (-O1 ke atas) itu bisa crash. Tanpa frame (NULL),
// longjmp cukup mengembalikan register, dan itu yang dibutuhkan di sini.
#if defined(_WIN64) && defined(__MINGW32__)
#define RT_SETJMP(b) _setjmp((b),NULL)
#else
#define RT_SETJMP(b) setjmp(b)
#endif
jmp_buf *rt_try_push(void);
void rt_try_pop(int n);
int rt_depth(void);
void rt_catch(int catchPc, int savedDepth);

// program
void rt_body(int pc, int (*fn)(void));
int rt_main(const char *mainPath, const char **names, const char **texts, int n, int (*top)(void));

#endif
