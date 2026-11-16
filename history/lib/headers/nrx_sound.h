// nrx_sound: library suara untuk nrxlang (WAV dan MP3)
// dipakai di script dengan:  lib nrx_sound.h
//   playSound("koin.wav")     putar efek suara sekali (beberapa efek bisa bersamaan)
//   playMusic("lagu.mp3")     putar musik berulang terus (menggantikan musik sebelumnya)
//   stopMusic()               hentikan musik
//   stopSound()               hentikan semua efek suara
#ifndef NRX_SOUND_H
#define NRX_SOUND_H

#include "nrxlang.h"

int nrxSoundOwns(const char *name);
Value nrxSoundCall(const char *name, Value *args, int nargs);

#endif
