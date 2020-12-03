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
