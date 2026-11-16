// main.c: titik masuk nrxlang.exe. Semua isi interpreter ada di libnrxrt.a (nrxlang.c dan
// library), yang juga dipakai program hasil "nrxlang build" dengan main()-nya sendiri.
int nrxMain(int argc, char **argv);
void nrx3dInit(void);

int main(int argc, char **argv){
    nrx3dInit();
    return nrxMain(argc,argv);
}
