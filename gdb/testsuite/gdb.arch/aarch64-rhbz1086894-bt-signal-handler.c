/* Copyright 2014 Free Software Foundation, Inc.

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

/* Internal test for RHEL-7.1.  */

#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>

static void
handle_alrm(int signo)
{
  kill (getpid (), SIGSEGV);
  assert (0);
}

int
main (int argc, char *argv[])
{
  signal (SIGALRM, handle_alrm);
  alarm (1);
  pause ();
  assert (0);
  return 0;
}
