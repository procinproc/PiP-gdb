#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

int main (int argc, char **argv)
{
  pid_t pid = 0;
  pid_t ppid;
  char buf[256];

  if (argc != 4)
    {
      fprintf (stderr, "Syntax: %s {standard|detached} <gcore command> <core output file>\n",
	       argv[0]);
      exit (1);
    }

  pid = fork ();

  switch (pid)
    {
      case 0:
        if (strcmp (argv[1], "detached") == 0)
	  setpgrp ();
	ppid = getppid ();
	sprintf (buf, "sh %s -o %s %d", argv[2], argv[3], (int) ppid);
	system (buf);
	kill (ppid, SIGTERM);
	break;

      case -1:
	perror ("fork err\n");
	exit (1);
	break;

      default:
	sleep (60);
    }

  return 0;
}
