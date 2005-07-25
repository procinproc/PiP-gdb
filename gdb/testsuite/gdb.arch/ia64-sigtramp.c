#include <stdio.h>
#include <signal.h>

int *l;

void x (int sig)
{
  printf ("in signal handler for signal %d\n", sig);
}

int main()
{
  int k;

  signal (SIGSEGV, &x);

  k = *l;

  printf ("k is %d\n", k);
 
  return 0;
}

