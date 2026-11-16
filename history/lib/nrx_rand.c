#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "nrx_rand.h"

static int seeded=0;

static void seedOnce(){
    if(!seeded){
        srand((unsigned)time(NULL) ^ (unsigned)clock());
        seeded=1;
    }
}

// rand() di Windows cuma sampai 32767, jadi digabung dua kali supaya dapat 30 bit
static unsigned bigRand(){
    return (((unsigned)rand()<<15) ^ (unsigned)rand()) & 0x3FFFFFFF;
}

int nrxRandOwns(const char *name){
    return !strcmp(name,"random") || !strcmp(name,"randomSeed") || !strcmp(name,"choice");
}

Value nrxRandCall(const char *name, Value *a, int n){
    seedOnce();
    if(strcmp(name,"random")==0){
        if(n==0) return valDec(bigRand()/(double)(1u<<30));
        if(n!=2) nrxFail("random butuh 0 atau 2 argumen: random() atau random(min, max)");
        double lo, hi;
        if(!numLike(a[0],&lo) || !numLike(a[1],&hi)) nrxFail("random(min, max) butuh angka");
        long long l=(long long)lo, h=(long long)hi;
        if(l>h){
            long long tmp=l;
            l=h;
            h=tmp;
        }
        return valNum((double)(l+(long long)(bigRand()%(unsigned long long)(h-l+1))));
    }
    if(strcmp(name,"randomSeed")==0){
        if(n!=1) nrxFail("randomSeed butuh 1 argumen: randomSeed(angka)");
        srand((unsigned)valToNum(a[0]));
        seeded=1;
        return valStr("");
    }
    // choice(list)
    if(n!=1 || a[0].type!=T_LIST) nrxFail("choice butuh 1 list: choice([\"a\", \"b\"])");
    if(a[0].list->count==0) nrxFail("choice: list masih kosong");
    return valCopy(a[0].list->items[bigRand()%a[0].list->count]);
}
