/* Target-dependent code for GNU/Linux, architecture independent.

   Copyright (C) 2009-2013 Free Software Foundation, Inc.

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

#ifndef LINUX_TDEP_H
#define LINUX_TDEP_H

#include "bfd.h"

struct regcache;

typedef char *(*linux_collect_thread_registers_ftype) (const struct regcache *,
						       ptid_t,
						       bfd *, char *, int *,
						       enum gdb_signal);

char *linux_make_corefile_notes (struct gdbarch *, bfd *, int *,
                                 linux_collect_thread_registers_ftype);

struct type *linux_get_siginfo_type (struct gdbarch *);

extern void linux_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch);

#ifdef ENABLE_PIP

extern int get_pip_process (pid_t pid, char *dest_name, size_t size,
			    ULONGEST *dest_addr);
extern int found_pc_in_symbol (pid_t pid, ULONGEST addr);
extern int check_pip (pid_t pid);

extern void linux_pip_scan (void);

/* Nonzero if exec_path is not same with /proc/PID/exe  */
extern ULONGEST pip_start_address;

#endif /* ENABLE_PIP */

#endif /* linux-tdep.h */
