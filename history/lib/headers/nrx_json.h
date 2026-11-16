// nrx_json: library JSON untuk nrxlang
// dipakai di script dengan:  lib nrx_json.h
//   toJson(data)          ubah list/map/angka/teks jadi teks JSON satu baris
//   toJson(data, true)    sama, tapi rapi (berbaris dan menjorok)
//   fromJson(teks)        ubah teks JSON jadi data nrxl (objek -> map, array -> list)
// cocok dipakai bersama nrx_crudfile: writeFile("data.json", toJson(data)) / fromJson(readFile("data.json"))
#ifndef NRX_JSON_H
#define NRX_JSON_H

#include "nrxlang.h"

int nrxJsonOwns(const char *name);
Value nrxJsonCall(const char *name, Value *args, int nargs);

#endif
