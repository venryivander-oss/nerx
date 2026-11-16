// nrx_rand: library angka acak untuk nrxlang
// dipakai di script dengan:  lib nrx_rand.h
//   random()            angka desimal acak 0 sampai di bawah 1
//   random(min, max)    bilangan bulat acak min..max (termasuk min dan max)
//   choice(list)        satu isi list yang dipilih acak
//   randomSeed(n)       atur seed supaya urutan acaknya bisa diulang
#ifndef NRX_RAND_H
#define NRX_RAND_H

#include "nrxlang.h"

int nrxRandOwns(const char *name);
Value nrxRandCall(const char *name, Value *args, int nargs);

#endif
