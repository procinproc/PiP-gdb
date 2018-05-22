/*
  * $RIKEN_copyright:$
  * $PIP_VERSION:$
  * $PIP_license:$
*/
/*
  * Written by Atsushi HORI <ahori@riken.jp>, 2016
*/

#include <pip.h>
#include <pip_debug.h> /* DBGF */
#include <unistd.h>

/* from PIP/test/test.h */
#define PRINT_FL(FSTR,V)	\
  fprintf(stderr,"%s:%d %s=%d\n",__FILE__,__LINE__,FSTR,V)
#define TESTINT(F)		\
  do{int __xyz=(F); if(__xyz){PRINT_FL(#F,__xyz);exit(9);}} while(0)

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
