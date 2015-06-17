/* Copyright 2015 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <pthread.h>
#include <unistd.h>

volatile int call_exit;
static pthread_barrier_t barrier;
#define NUMTHREADS 256

void *
thread_function (void *arg)
{
  pthread_barrier_wait (&barrier);

  while (!call_exit)
    usleep (1);

  _exit (0);
  return NULL;
}

void
all_threads_started (void)
{
  call_exit = 1;
}

int
main (int argc, char **argv)
{
  pthread_t threads[NUMTHREADS];
  int i;

  pthread_barrier_init (&barrier, NULL, NUMTHREADS + 1);

  for (i = 0; i < NUMTHREADS; ++i)
    pthread_create (&threads[i], NULL, thread_function, NULL);

  pthread_barrier_wait (&barrier);

  all_threads_started ();

  for (i = 0; i < NUMTHREADS; ++i)
    pthread_join (threads[i], NULL);

  return 0;
}
