/*
 * $RIKEN_copyright:$
 * $PIP_VERSION:$
 * $PIP_license:$
 */
/*
 * Written by Atsushi HORI <ahori@riken.jp>, 2016
 */

#include <unistd.h>
#include <stdio.h>

int test=20;

void foo() {
  printf("Foo...\n");
  printf("Bar...\n");
}

int main() {
  printf("Sleeping...\n");
  sleep(30);
  foo();
  printf("Done...\n");
  return 0;
}
