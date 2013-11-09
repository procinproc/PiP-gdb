/* This testcase is part of GDB, the GNU debugger.

   Copyright 2013 Free Software Foundation, Inc.

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

#ifdef DEBUG
#include <stdio.h>
#endif

class A
{
public:
  int i;
  int z;
  A () : i (1), z (10) {}
};

class B : public virtual A
{
public:
  int i;
  B () : i (2) {}
};

class C : public virtual A
{
public:
  int i;
  int c;
  C () : i (3), c (30) {}
};

class D : public B, public C
{
public:
  int i;
  int x;
  D () : i (4), x (40) {}

#ifdef DEBUG
#define SUM(X)					\
  do						\
    {						\
      sum += (X);				\
      printf ("" #X " = %d\n", (X));		\
    }						\
  while (0)
#else
#define SUM(X) sum += (X)
#endif

int
f (void)
  {
    int sum = 0;

    SUM (i);
    SUM (D::i);
    SUM (D::B::i);
    SUM (B::i);
    SUM (D::C::i);
    SUM (C::i);
    SUM (D::B::A::i);
    SUM (B::A::i);
    SUM (A::i);
    SUM (D::C::A::i);
    SUM (C::A::i);
    SUM (D::x);
    SUM (x);
    SUM (D::C::c);
    SUM (C::c);
    SUM (c);

    return sum;
  }
};

int
main (void)
{
  D d;

  return d.f ();
}
