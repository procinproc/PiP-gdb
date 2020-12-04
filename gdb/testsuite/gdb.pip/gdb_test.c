/*
   $RIKEN_copyright: 2018 Riken Center for Computational Sceience,
	  System Software Devlopment Team. All rights researved$

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
/*
  * Written by Atsushi HORI <ahori@riken.jp>, 2016
*/

#include <pip/pip.h>
#ifdef AH
#include <pip/pip_debug.h> /* DBGF */
#else
#ifdef DEBUG
#define DBGF(...)  \
  do { fprintf(stderr,__VA_ARGS__); fprintf( stderr, "\n" ); } while(0)
#endif
#endif
#include <unistd.h>

/* from PIP/test/test.h */
#define PRINT_FL(FSTR,V)	\
  fprintf(stderr,"%s:%d %s=%d\n",__FILE__,__LINE__,FSTR,V)
#define TESTINT(F)		\
  do{int __xyz=(F); if(__xyz){PRINT_FL(#F,__xyz);_exit(9);}} while(0)

int root_exp = 0;

void *thread1(void *args)
{
  int inf = 1;
  while (inf) {
    sleep(1);
  }
  return NULL;
}

void *thread2(void *args)
{
  int inf = 1;
  while (inf) {
    sleep(1);
  }
  return NULL;
}

int main( int argc, char **argv ) {
  int pipid, ntasks;
  int i;
  int err;
  int retval;
  pthread_t t1, t2;

  ntasks = PIP_NTASKS_MAX;
  TESTINT( pip_init( &pipid, &ntasks, NULL, 0 ) );

  pthread_create(&t1, NULL, thread1, (void *)NULL);
  pthread_create(&t2, NULL, thread2, (void *)NULL);

  if( pipid == PIP_PIPID_ROOT ) {
    root_exp = 1;
    char *sleep_argv[] = {
	    "./gdb.pip/sleep",
	    NULL,
    };

    i = 0;
    pipid = 0;
    err = pip_spawn( sleep_argv[0], sleep_argv, NULL, i%4, &pipid, NULL, NULL, NULL );
    if( err ) {
      fprintf( stderr, "pip_spawn(%d!=%d)=%d !!!!!!\n", i, pipid, err );
    }

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    DBGF( "calling pip_wait(%d)", i );
    TESTINT( pip_wait( i, &retval ) );
    if( retval != i ) {
      fprintf( stderr, "[PIPID=%d] pip_wait() returns %d ???\n", i, retval );
    } else {
      fprintf( stderr, " terminated. OK\n" );
    }

    TESTINT( pip_fin() );

  } else {
    root_exp = 999;
    fprintf( stderr, "Hello, I am PIPID[%d] ...", pipid );
    sleep(120);
    pip_exit( pipid );
  }
  return 0;
}
