// nrx_keyboard: library keyboard console untuk nrxlang (tanpa window, tanpa menekan Enter)
// dipakai di script dengan:  lib nrx_keyboard.h
//   keyPressed("left")   true kalau tombol itu ditekan (atau sedang ditahan) sejak dicek terakhir
//                        nama tombol: "a".."z", "0".."9", "left", "right", "up", "down",
//                        "space", "enter", "escape", "tab", "backspace", "f1".."f12", dll.
//   getKey()             tunggu sampai satu tombol ditekan, lalu kembalikan namanya (mis. "a", "enter")
#ifndef NRX_KEYBOARD_H
#define NRX_KEYBOARD_H

#include "nrxlang.h"

int nrxKeyboardOwns(const char *name);
Value nrxKeyboardCall(const char *name, Value *args, int nargs);

#endif
