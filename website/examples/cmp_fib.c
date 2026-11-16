#include <stdio.h>
#include <windows.h>
long long fib(long long n){ if(n<2) return n; return fib(n-1)+fib(n-2); }
int main(void){ LARGE_INTEGER f,a,b; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
 long long r=fib(38); QueryPerformanceCounter(&b); printf("%lld\n",r);
 printf("done in: %.2f ms\n",(double)(b.QuadPart-a.QuadPart)*1000.0/f.QuadPart); return 0; }
