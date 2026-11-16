#include <stdio.h>
#include <windows.h>
int main(void){
  LARGE_INTEGER f,a,b; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
  volatile long long s=0; for(long long i=0;i<3000000;i++){ s += (i*i) % 7; }
  QueryPerformanceCounter(&b);
  printf("%lld\n",(long long)s);
  printf("done in: %.2f ms\n", (double)(b.QuadPart-a.QuadPart)*1000.0/f.QuadPart);
  return 0; }
