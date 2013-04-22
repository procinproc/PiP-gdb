/* Read ELF (Executable and Linking Format) object files for GDB.

   Copyright (C) 1991-2013 Free Software Foundation, Inc.

   Written by Fred Fish at Cygnus Support.

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

#include "defs.h"
#include "bfd.h"
#include "gdb_string.h"
#include "elf-bfd.h"
#include "elf/common.h"
#include "elf/internal.h"
#include "elf/mips.h"
#include "symtab.h"
#include "symfile.h"
#include "objfiles.h"
#include "buildsym.h"
#include "stabsread.h"
#include "gdb-stabs.h"
#include "complaints.h"
#include "demangle.h"
#include "psympriv.h"
#include "filenames.h"
#include "probe.h"
#include "arch-utils.h"
#include "gdbtypes.h"
#include "value.h"
#include "infcall.h"
#include "gdbthread.h"
#include "regcache.h"
#include "bcache.h"
#include "gdb_bfd.h"
#include "libbfd.h"
#include "gdbcore.h"
#include "gdbcmd.h"
#include "observer.h"
#include "elf/external.h"
#include <sys/stat.h>

extern void _initialize_elfread (void);

/* Forward declarations.  */
static const struct sym_fns elf_sym_fns_gdb_index;
static const struct sym_fns elf_sym_fns_lazy_psyms;

/* The struct elfinfo is available only during ELF symbol table and
   psymtab reading.  It is destroyed at the completion of psymtab-reading.
   It's local to elf_symfile_read.  */

struct elfinfo
  {
    asection *stabsect;		/* Section pointer for .stab section */
    asection *stabindexsect;	/* Section pointer for .stab.index section */
    asection *mdebugsect;	/* Section pointer for .mdebug section */
  };

/* Per-objfile data for probe info.  */

static const struct objfile_data *probe_key = NULL;

static void free_elfinfo (void *);

/* Minimal symbols located at the GOT entries for .plt - that is the real
   pointer where the given entry will jump to.  It gets updated by the real
   function address during lazy ld.so resolving in the inferior.  These
   minimal symbols are indexed for <tab>-completion.  */

#define SYMBOL_GOT_PLT_SUFFIX "@got.plt"

/* Locate the segments in ABFD.  */

static struct symfile_segment_data *
elf_symfile_segments (bfd *abfd)
{
  Elf_Internal_Phdr *phdrs, **segments;
  long phdrs_size;
  int num_phdrs, num_segments, num_sections, i;
  asection *sect;
  struct symfile_segment_data *data;

  phdrs_size = bfd_get_elf_phdr_upper_bound (abfd);
  if (phdrs_size == -1)
    return NULL;

  phdrs = alloca (phdrs_size);
  num_phdrs = bfd_get_elf_phdrs (abfd, phdrs);
  if (num_phdrs == -1)
    return NULL;

  num_segments = 0;
  segments = alloca (sizeof (Elf_Internal_Phdr *) * num_phdrs);
  for (i = 0; i < num_phdrs; i++)
    if (phdrs[i].p_type == PT_LOAD)
      segments[num_segments++] = &phdrs[i];

  if (num_segments == 0)
    return NULL;

  data = XZALLOC (struct symfile_segment_data);
  data->num_segments = num_segments;
  data->segment_bases = XCALLOC (num_segments, CORE_ADDR);
  data->segment_sizes = XCALLOC (num_segments, CORE_ADDR);

  for (i = 0; i < num_segments; i++)
    {
      data->segment_bases[i] = segments[i]->p_vaddr;
      data->segment_sizes[i] = segments[i]->p_memsz;
    }

  num_sections = bfd_count_sections (abfd);
  data->segment_info = XCALLOC (num_sections, int);

  for (i = 0, sect = abfd->sections; sect != NULL; i++, sect = sect->next)
    {
      int j;
      CORE_ADDR vma;

      if ((bfd_get_section_flags (abfd, sect) & SEC_ALLOC) == 0)
	continue;

      vma = bfd_get_section_vma (abfd, sect);

      for (j = 0; j < num_segments; j++)
	if (segments[j]->p_memsz > 0
	    && vma >= segments[j]->p_vaddr
	    && (vma - segments[j]->p_vaddr) < segments[j]->p_memsz)
	  {
	    data->segment_info[i] = j + 1;
	    break;
	  }

      /* We should have found a segment for every non-empty section.
	 If we haven't, we will not relocate this section by any
	 offsets we apply to the segments.  As an exception, do not
	 warn about SHT_NOBITS sections; in normal ELF execution
	 environments, SHT_NOBITS means zero-initialized and belongs
	 in a segment, but in no-OS environments some tools (e.g. ARM
	 RealView) use SHT_NOBITS for uninitialized data.  Since it is
	 uninitialized, it doesn't need a program header.  Such
	 binaries are not relocatable.  */
      if (bfd_get_section_size (sect) > 0 && j == num_segments
	  && (bfd_get_section_flags (abfd, sect) & SEC_LOAD) != 0)
	warning (_("Loadable section \"%s\" outside of ELF segments"),
		 bfd_section_name (abfd, sect));
    }

  return data;
}

/* We are called once per section from elf_symfile_read.  We
   need to examine each section we are passed, check to see
   if it is something we are interested in processing, and
   if so, stash away some access information for the section.

   For now we recognize the dwarf debug information sections and
   line number sections from matching their section names.  The
   ELF definition is no real help here since it has no direct
   knowledge of DWARF (by design, so any debugging format can be
   used).

   We also recognize the ".stab" sections used by the Sun compilers
   released with Solaris 2.

   FIXME: The section names should not be hardwired strings (what
   should they be?  I don't think most object file formats have enough
   section flags to specify what kind of debug section it is.
   -kingdon).  */

static void
elf_locate_sections (bfd *ignore_abfd, asection *sectp, void *eip)
{
  struct elfinfo *ei;

  ei = (struct elfinfo *) eip;
  if (strcmp (sectp->name, ".stab") == 0)
    {
      ei->stabsect = sectp;
    }
  else if (strcmp (sectp->name, ".stab.index") == 0)
    {
      ei->stabindexsect = sectp;
    }
  else if (strcmp (sectp->name, ".mdebug") == 0)
    {
      ei->mdebugsect = sectp;
    }
}

static struct minimal_symbol *
record_minimal_symbol (const char *name, int name_len, int copy_name,
		       CORE_ADDR address,
		       enum minimal_symbol_type ms_type,
		       asection *bfd_section, struct objfile *objfile)
{
  struct gdbarch *gdbarch = get_objfile_arch (objfile);

  if (ms_type == mst_text || ms_type == mst_file_text
      || ms_type == mst_text_gnu_ifunc)
    address = gdbarch_addr_bits_remove (gdbarch, address);

  return prim_record_minimal_symbol_full (name, name_len, copy_name, address,
					  ms_type, bfd_section->index,
					  bfd_section, objfile);
}

/* Read the symbol table of an ELF file.

   Given an objfile, a symbol table, and a flag indicating whether the
   symbol table contains regular, dynamic, or synthetic symbols, add all
   the global function and data symbols to the minimal symbol table.

   In stabs-in-ELF, as implemented by Sun, there are some local symbols
   defined in the ELF symbol table, which can be used to locate
   the beginnings of sections from each ".o" file that was linked to
   form the executable objfile.  We gather any such info and record it
   in data structures hung off the objfile's private data.  */

#define ST_REGULAR 0
#define ST_DYNAMIC 1
#define ST_SYNTHETIC 2

static void
elf_symtab_read (struct objfile *objfile, int type,
		 long number_of_symbols, asymbol **symbol_table,
		 int copy_names)
{
  struct gdbarch *gdbarch = get_objfile_arch (objfile);
  asymbol *sym;
  long i;
  CORE_ADDR symaddr;
  CORE_ADDR offset;
  enum minimal_symbol_type ms_type;
  /* If sectinfo is nonNULL, it contains section info that should end up
     filed in the objfile.  */
  struct stab_section_info *sectinfo = NULL;
  /* If filesym is nonzero, it points to a file symbol, but we haven't
     seen any section info for it yet.  */
  asymbol *filesym = 0;
  /* Name of filesym.  This is either a constant string or is saved on
     the objfile's filename cache.  */
  const char *filesymname = "";
  struct dbx_symfile_info *dbx = DBX_SYMFILE_INFO (objfile);
  int stripped = (bfd_get_symcount (objfile->obfd) == 0);

  for (i = 0; i < number_of_symbols; i++)
    {
      sym = symbol_table[i];
      if (sym->name == NULL || *sym->name == '\0')
	{
	  /* Skip names that don't exist (shouldn't happen), or names
	     that are null strings (may happen).  */
	  continue;
	}

      /* Skip "special" symbols, e.g. ARM mapping symbols.  These are
	 symbols which do not correspond to objects in the symbol table,
	 but have some other target-specific meaning.  */
      if (bfd_is_target_special_symbol (objfile->obfd, sym))
	{
	  if (gdbarch_record_special_symbol_p (gdbarch))
	    gdbarch_record_special_symbol (gdbarch, objfile, sym);
	  continue;
	}

      offset = ANOFFSET (objfile->section_offsets, sym->section->index);
      if (type == ST_DYNAMIC
	  && sym->section == bfd_und_section_ptr
	  && (sym->flags & BSF_FUNCTION))
	{
	  struct minimal_symbol *msym;
	  bfd *abfd = objfile->obfd;
	  asection *sect;

	  /* Symbol is a reference to a function defined in
	     a shared library.
	     If its value is non zero then it is usually the address
	     of the corresponding entry in the procedure linkage table,
	     plus the desired section offset.
	     If its value is zero then the dynamic linker has to resolve
	     the symbol.  We are unable to find any meaningful address
	     for this symbol in the executable file, so we skip it.  */
	  symaddr = sym->value;
	  if (symaddr == 0)
	    continue;

	  /* sym->section is the undefined section.  However, we want to
	     record the section where the PLT stub resides with the
	     minimal symbol.  Search the section table for the one that
	     covers the stub's address.  */
	  for (sect = abfd->sections; sect != NULL; sect = sect->next)
	    {
	      if ((bfd_get_section_flags (abfd, sect) & SEC_ALLOC) == 0)
		continue;

	      if (symaddr >= bfd_get_section_vma (abfd, sect)
		  && symaddr < bfd_get_section_vma (abfd, sect)
			       + bfd_get_section_size (sect))
		break;
	    }
	  if (!sect)
	    continue;

	  /* On ia64-hpux, we have discovered that the system linker
	     adds undefined symbols with nonzero addresses that cannot
	     be right (their address points inside the code of another
	     function in the .text section).  This creates problems
	     when trying to determine which symbol corresponds to
	     a given address.

	     We try to detect those buggy symbols by checking which
	     section we think they correspond to.  Normally, PLT symbols
	     are stored inside their own section, and the typical name
	     for that section is ".plt".  So, if there is a ".plt"
	     section, and yet the section name of our symbol does not
	     start with ".plt", we ignore that symbol.  */
	  if (strncmp (sect->name, ".plt", 4) != 0
	      && bfd_get_section_by_name (abfd, ".plt") != NULL)
	    continue;

	  symaddr += ANOFFSET (objfile->section_offsets, sect->index);

	  msym = record_minimal_symbol
	    (sym->name, strlen (sym->name), copy_names,
	     symaddr, mst_solib_trampoline, sect, objfile);
	  if (msym != NULL)
	    msym->filename = filesymname;
	  continue;
	}

      /* If it is a nonstripped executable, do not enter dynamic
	 symbols, as the dynamic symbol table is usually a subset
	 of the main symbol table.  */
      if (type == ST_DYNAMIC && !stripped)
	continue;
      if (sym->flags & BSF_FILE)
	{
	  /* STT_FILE debugging symbol that helps stabs-in-elf debugging.
	     Chain any old one onto the objfile; remember new sym.  */
	  if (sectinfo != NULL)
	    {
	      sectinfo->next = dbx->stab_section_info;
	      dbx->stab_section_info = sectinfo;
	      sectinfo = NULL;
	    }
	  filesym = sym;
	  filesymname = bcache (filesym->name, strlen (filesym->name) + 1,
				objfile->per_bfd->filename_cache);
	}
      else if (sym->flags & BSF_SECTION_SYM)
	continue;
      else if (sym->flags & (BSF_GLOBAL | BSF_LOCAL | BSF_WEAK
			     | BSF_GNU_UNIQUE))
	{
	  struct minimal_symbol *msym;

	  /* Select global/local/weak symbols.  Note that bfd puts abs
	     symbols in their own section, so all symbols we are
	     interested in will have a section.  */
	  /* Bfd symbols are section relative.  */
	  symaddr = sym->value + sym->section->vma;
	  /* Relocate all non-absolute and non-TLS symbols by the
	     section offset.  */
	  if (sym->section != bfd_abs_section_ptr
	      && !(sym->section->flags & SEC_THREAD_LOCAL))
	    {
	      symaddr += offset;
	    }
	  /* For non-absolute symbols, use the type of the section
	     they are relative to, to intuit text/data.  Bfd provides
	     no way of figuring this out for absolute symbols.  */
	  if (sym->section == bfd_abs_section_ptr)
	    {
	      /* This is a hack to get the minimal symbol type
		 right for Irix 5, which has absolute addresses
		 with special section indices for dynamic symbols.

		 NOTE: uweigand-20071112: Synthetic symbols do not
		 have an ELF-private part, so do not touch those.  */
	      unsigned int shndx = type == ST_SYNTHETIC ? 0 :
		((elf_symbol_type *) sym)->internal_elf_sym.st_shndx;

	      switch (shndx)
		{
		case SHN_MIPS_TEXT:
		  ms_type = mst_text;
		  break;
		case SHN_MIPS_DATA:
		  ms_type = mst_data;
		  break;
		case SHN_MIPS_ACOMMON:
		  ms_type = mst_bss;
		  break;
		default:
		  ms_type = mst_abs;
		}

	      /* If it is an Irix dynamic symbol, skip section name
		 symbols, relocate all others by section offset.  */
	      if (ms_type != mst_abs)
		{
		  if (sym->name[0] == '.')
		    continue;
		  symaddr += offset;
		}
	    }
	  else if (sym->section->flags & SEC_CODE)
	    {
	      if (sym->flags & (BSF_GLOBAL | BSF_WEAK | BSF_GNU_UNIQUE))
		{
		  if (sym->flags & BSF_GNU_INDIRECT_FUNCTION)
		    ms_type = mst_text_gnu_ifunc;
		  else
		    ms_type = mst_text;
		}
	      /* The BSF_SYNTHETIC check is there to omit ppc64 function
		 descriptors mistaken for static functions starting with 'L'.
		 */
	      else if ((sym->name[0] == '.' && sym->name[1] == 'L'
			&& (sym->flags & BSF_SYNTHETIC) == 0)
		       || ((sym->flags & BSF_LOCAL)
			   && sym->name[0] == '$'
			   && sym->name[1] == 'L'))
		/* Looks like a compiler-generated label.  Skip
		   it.  The assembler should be skipping these (to
		   keep executables small), but apparently with
		   gcc on the (deleted) delta m88k SVR4, it loses.
		   So to have us check too should be harmless (but
		   I encourage people to fix this in the assembler
		   instead of adding checks here).  */
		continue;
	      else
		{
		  ms_type = mst_file_text;
		}
	    }
	  else if (sym->section->flags & SEC_ALLOC)
	    {
	      if (sym->flags & (BSF_GLOBAL | BSF_WEAK | BSF_GNU_UNIQUE))
		{
		  if (sym->section->flags & SEC_LOAD)
		    {
		      ms_type = mst_data;
		    }
		  else
		    {
		      ms_type = mst_bss;
		    }
		}
	      else if (sym->flags & BSF_LOCAL)
		{
		  /* Named Local variable in a Data section.
		     Check its name for stabs-in-elf.  */
		  int special_local_sect;

		  if (strcmp ("Bbss.bss", sym->name) == 0)
		    special_local_sect = SECT_OFF_BSS (objfile);
		  else if (strcmp ("Ddata.data", sym->name) == 0)
		    special_local_sect = SECT_OFF_DATA (objfile);
		  else if (strcmp ("Drodata.rodata", sym->name) == 0)
		    special_local_sect = SECT_OFF_RODATA (objfile);
		  else
		    special_local_sect = -1;
		  if (special_local_sect >= 0)
		    {
		      /* Found a special local symbol.  Allocate a
			 sectinfo, if needed, and fill it in.  */
		      if (sectinfo == NULL)
			{
			  int max_index;
			  size_t size;

			  max_index = SECT_OFF_BSS (objfile);
			  if (objfile->sect_index_data > max_index)
			    max_index = objfile->sect_index_data;
			  if (objfile->sect_index_rodata > max_index)
			    max_index = objfile->sect_index_rodata;

			  /* max_index is the largest index we'll
			     use into this array, so we must
			     allocate max_index+1 elements for it.
			     However, 'struct stab_section_info'
			     already includes one element, so we
			     need to allocate max_index aadditional
			     elements.  */
			  size = (sizeof (struct stab_section_info)
				  + (sizeof (CORE_ADDR) * max_index));
			  sectinfo = (struct stab_section_info *)
			    xmalloc (size);
			  memset (sectinfo, 0, size);
			  sectinfo->num_sections = max_index;
			  if (filesym == NULL)
			    {
			      complaint (&symfile_complaints,
					 _("elf/stab section information %s "
					   "without a preceding file symbol"),
					 sym->name);
			    }
			  else
			    {
			      sectinfo->filename =
				(char *) filesym->name;
			    }
			}
		      if (sectinfo->sections[special_local_sect] != 0)
			complaint (&symfile_complaints,
				   _("duplicated elf/stab section "
				     "information for %s"),
				   sectinfo->filename);
		      /* BFD symbols are section relative.  */
		      symaddr = sym->value + sym->section->vma;
		      /* Relocate non-absolute symbols by the
			 section offset.  */
		      if (sym->section != bfd_abs_section_ptr)
			symaddr += offset;
		      sectinfo->sections[special_local_sect] = symaddr;
		      /* The special local symbols don't go in the
			 minimal symbol table, so ignore this one.  */
		      continue;
		    }
		  /* Not a special stabs-in-elf symbol, do regular
		     symbol processing.  */
		  if (sym->section->flags & SEC_LOAD)
		    {
		      ms_type = mst_file_data;
		    }
		  else
		    {
		      ms_type = mst_file_bss;
		    }
		}
	      else
		{
		  ms_type = mst_unknown;
		}
	    }
	  else
	    {
	      /* FIXME:  Solaris2 shared libraries include lots of
		 odd "absolute" and "undefined" symbols, that play
		 hob with actions like finding what function the PC
		 is in.  Ignore them if they aren't text, data, or bss.  */
	      /* ms_type = mst_unknown; */
	      continue;	/* Skip this symbol.  */
	    }
	  msym = record_minimal_symbol
	    (sym->name, strlen (sym->name), copy_names, symaddr,
	     ms_type, sym->section, objfile);

	  if (msym)
	    {
	      /* NOTE: uweigand-20071112: A synthetic symbol does not have an
		 ELF-private part.  */
	      if (type != ST_SYNTHETIC)
		{
		  /* Pass symbol size field in via BFD.  FIXME!!!  */
		  elf_symbol_type *elf_sym = (elf_symbol_type *) sym;
		  SET_MSYMBOL_SIZE (msym, elf_sym->internal_elf_sym.st_size);
		}

	      msym->filename = filesymname;
	      gdbarch_elf_make_msymbol_special (gdbarch, sym, msym);
	    }

	  /* For @plt symbols, also record a trampoline to the
	     destination symbol.  The @plt symbol will be used in
	     disassembly, and the trampoline will be used when we are
	     trying to find the target.  */
	  if (msym && ms_type == mst_text && type == ST_SYNTHETIC)
	    {
	      int len = strlen (sym->name);

	      if (len > 4 && strcmp (sym->name + len - 4, "@plt") == 0)
		{
		  struct minimal_symbol *mtramp;

		  mtramp = record_minimal_symbol (sym->name, len - 4, 1,
						  symaddr,
						  mst_solib_trampoline,
						  sym->section, objfile);
		  if (mtramp)
		    {
		      SET_MSYMBOL_SIZE (mtramp, MSYMBOL_SIZE (msym));
		      mtramp->created_by_gdb = 1;
		      mtramp->filename = filesymname;
		      gdbarch_elf_make_msymbol_special (gdbarch, sym, mtramp);
		    }
		}
	    }
	}
    }
}

/* Build minimal symbols named `function@got.plt' (see SYMBOL_GOT_PLT_SUFFIX)
   for later look ups of which function to call when user requests
   a STT_GNU_IFUNC function.  As the STT_GNU_IFUNC type is found at the target
   library defining `function' we cannot yet know while reading OBJFILE which
   of the SYMBOL_GOT_PLT_SUFFIX entries will be needed and later
   DYN_SYMBOL_TABLE is no longer easily available for OBJFILE.  */

static void
elf_rel_plt_read (struct objfile *objfile, asymbol **dyn_symbol_table)
{
  bfd *obfd = objfile->obfd;
  const struct elf_backend_data *bed = get_elf_backend_data (obfd);
  asection *plt, *relplt, *got_plt;
  int plt_elf_idx;
  bfd_size_type reloc_count, reloc;
  char *string_buffer = NULL;
  size_t string_buffer_size = 0;
  struct cleanup *back_to;
  struct gdbarch *gdbarch = objfile->gdbarch;
  struct type *ptr_type = builtin_type (gdbarch)->builtin_data_ptr;
  size_t ptr_size = TYPE_LENGTH (ptr_type);

  if (objfile->separate_debug_objfile_backlink)
    return;

  plt = bfd_get_section_by_name (obfd, ".plt");
  if (plt == NULL)
    return;
  plt_elf_idx = elf_section_data (plt)->this_idx;

  got_plt = bfd_get_section_by_name (obfd, ".got.plt");
  if (got_plt == NULL)
    return;

  /* This search algorithm is from _bfd_elf_canonicalize_dynamic_reloc.  */
  for (relplt = obfd->sections; relplt != NULL; relplt = relplt->next)
    if (elf_section_data (relplt)->this_hdr.sh_info == plt_elf_idx
	&& (elf_section_data (relplt)->this_hdr.sh_type == SHT_REL
	    || elf_section_data (relplt)->this_hdr.sh_type == SHT_RELA))
      break;
  if (relplt == NULL)
    return;

  if (! bed->s->slurp_reloc_table (obfd, relplt, dyn_symbol_table, TRUE))
    return;

  back_to = make_cleanup (free_current_contents, &string_buffer);

  reloc_count = relplt->size / elf_section_data (relplt)->this_hdr.sh_entsize;
  for (reloc = 0; reloc < reloc_count; reloc++)
    {
      const char *name;
      struct minimal_symbol *msym;
      CORE_ADDR address;
      const size_t got_suffix_len = strlen (SYMBOL_GOT_PLT_SUFFIX);
      size_t name_len;

      name = bfd_asymbol_name (*relplt->relocation[reloc].sym_ptr_ptr);
      name_len = strlen (name);
      address = relplt->relocation[reloc].address;

      /* Does the pointer reside in the .got.plt section?  */
      if (!(bfd_get_section_vma (obfd, got_plt) <= address
            && address < bfd_get_section_vma (obfd, got_plt)
			 + bfd_get_section_size (got_plt)))
	continue;

      /* We cannot check if NAME is a reference to mst_text_gnu_ifunc as in
	 OBJFILE the symbol is undefined and the objfile having NAME defined
	 may not yet have been loaded.  */

      if (string_buffer_size < name_len + got_suffix_len + 1)
	{
	  string_buffer_size = 2 * (name_len + got_suffix_len);
	  string_buffer = xrealloc (string_buffer, string_buffer_size);
	}
      memcpy (string_buffer, name, name_len);
      memcpy (&string_buffer[name_len], SYMBOL_GOT_PLT_SUFFIX,
	      got_suffix_len + 1);

      msym = record_minimal_symbol (string_buffer, name_len + got_suffix_len,
                                    1, address, mst_slot_got_plt, got_plt,
				    objfile);
      if (msym)
	SET_MSYMBOL_SIZE (msym, ptr_size);
    }

  do_cleanups (back_to);
}

/* The data pointer is htab_t for gnu_ifunc_record_cache_unchecked.  */

static const struct objfile_data *elf_objfile_gnu_ifunc_cache_data;

/* Map function names to CORE_ADDR in elf_objfile_gnu_ifunc_cache_data.  */

struct elf_gnu_ifunc_cache
{
  /* This is always a function entry address, not a function descriptor.  */
  CORE_ADDR addr;

  char name[1];
};

/* htab_hash for elf_objfile_gnu_ifunc_cache_data.  */

static hashval_t
elf_gnu_ifunc_cache_hash (const void *a_voidp)
{
  const struct elf_gnu_ifunc_cache *a = a_voidp;

  return htab_hash_string (a->name);
}

/* htab_eq for elf_objfile_gnu_ifunc_cache_data.  */

static int
elf_gnu_ifunc_cache_eq (const void *a_voidp, const void *b_voidp)
{
  const struct elf_gnu_ifunc_cache *a = a_voidp;
  const struct elf_gnu_ifunc_cache *b = b_voidp;

  return strcmp (a->name, b->name) == 0;
}

/* Record the target function address of a STT_GNU_IFUNC function NAME is the
   function entry address ADDR.  Return 1 if NAME and ADDR are considered as
   valid and therefore they were successfully recorded, return 0 otherwise.

   Function does not expect a duplicate entry.  Use
   elf_gnu_ifunc_resolve_by_cache first to check if the entry for NAME already
   exists.  */

static int
elf_gnu_ifunc_record_cache (const char *name, CORE_ADDR addr)
{
  struct minimal_symbol *msym;
  asection *sect;
  struct objfile *objfile;
  htab_t htab;
  struct elf_gnu_ifunc_cache entry_local, *entry_p;
  void **slot;

  msym = lookup_minimal_symbol_by_pc (addr);
  if (msym == NULL)
    return 0;
  if (SYMBOL_VALUE_ADDRESS (msym) != addr)
    return 0;
  /* minimal symbols have always SYMBOL_OBJ_SECTION non-NULL.  */
  sect = SYMBOL_OBJ_SECTION (msym)->the_bfd_section;
  objfile = SYMBOL_OBJ_SECTION (msym)->objfile;

  /* If .plt jumps back to .plt the symbol is still deferred for later
     resolution and it has no use for GDB.  Besides ".text" this symbol can
     reside also in ".opd" for ppc64 function descriptor.  */
  if (strcmp (bfd_get_section_name (objfile->obfd, sect), ".plt") == 0)
    return 0;

  htab = objfile_data (objfile, elf_objfile_gnu_ifunc_cache_data);
  if (htab == NULL)
    {
      htab = htab_create_alloc_ex (1, elf_gnu_ifunc_cache_hash,
				   elf_gnu_ifunc_cache_eq,
				   NULL, &objfile->objfile_obstack,
				   hashtab_obstack_allocate,
				   dummy_obstack_deallocate);
      set_objfile_data (objfile, elf_objfile_gnu_ifunc_cache_data, htab);
    }

  entry_local.addr = addr;
  obstack_grow (&objfile->objfile_obstack, &entry_local,
		offsetof (struct elf_gnu_ifunc_cache, name));
  obstack_grow_str0 (&objfile->objfile_obstack, name);
  entry_p = obstack_finish (&objfile->objfile_obstack);

  slot = htab_find_slot (htab, entry_p, INSERT);
  if (*slot != NULL)
    {
      struct elf_gnu_ifunc_cache *entry_found_p = *slot;
      struct gdbarch *gdbarch = objfile->gdbarch;

      if (entry_found_p->addr != addr)
	{
	  /* This case indicates buggy inferior program, the resolved address
	     should never change.  */

	    warning (_("gnu-indirect-function \"%s\" has changed its resolved "
		       "function_address from %s to %s"),
		     name, paddress (gdbarch, entry_found_p->addr),
		     paddress (gdbarch, addr));
	}

      /* New ENTRY_P is here leaked/duplicate in the OBJFILE obstack.  */
    }
  *slot = entry_p;

  return 1;
}

/* Try to find the target resolved function entry address of a STT_GNU_IFUNC
   function NAME.  If the address is found it is stored to *ADDR_P (if ADDR_P
   is not NULL) and the function returns 1.  It returns 0 otherwise.

   Only the elf_objfile_gnu_ifunc_cache_data hash table is searched by this
   function.  */

static int
elf_gnu_ifunc_resolve_by_cache (const char *name, CORE_ADDR *addr_p)
{
  struct objfile *objfile;

  ALL_PSPACE_OBJFILES (current_program_space, objfile)
    {
      htab_t htab;
      struct elf_gnu_ifunc_cache *entry_p;
      void **slot;

      htab = objfile_data (objfile, elf_objfile_gnu_ifunc_cache_data);
      if (htab == NULL)
	continue;

      entry_p = alloca (sizeof (*entry_p) + strlen (name));
      strcpy (entry_p->name, name);

      slot = htab_find_slot (htab, entry_p, NO_INSERT);
      if (slot == NULL)
	continue;
      entry_p = *slot;
      gdb_assert (entry_p != NULL);

      if (addr_p)
	*addr_p = entry_p->addr;
      return 1;
    }

  return 0;
}

/* Try to find the target resolved function entry address of a STT_GNU_IFUNC
   function NAME.  If the address is found it is stored to *ADDR_P (if ADDR_P
   is not NULL) and the function returns 1.  It returns 0 otherwise.

   Only the SYMBOL_GOT_PLT_SUFFIX locations are searched by this function.
   elf_gnu_ifunc_resolve_by_cache must have been already called for NAME to
   prevent cache entries duplicates.  */

static int
elf_gnu_ifunc_resolve_by_got (const char *name, CORE_ADDR *addr_p)
{
  char *name_got_plt;
  struct objfile *objfile;
  const size_t got_suffix_len = strlen (SYMBOL_GOT_PLT_SUFFIX);

  name_got_plt = alloca (strlen (name) + got_suffix_len + 1);
  sprintf (name_got_plt, "%s" SYMBOL_GOT_PLT_SUFFIX, name);

  ALL_PSPACE_OBJFILES (current_program_space, objfile)
    {
      bfd *obfd = objfile->obfd;
      struct gdbarch *gdbarch = objfile->gdbarch;
      struct type *ptr_type = builtin_type (gdbarch)->builtin_data_ptr;
      size_t ptr_size = TYPE_LENGTH (ptr_type);
      CORE_ADDR pointer_address, addr;
      asection *plt;
      gdb_byte *buf = alloca (ptr_size);
      struct minimal_symbol *msym;

      msym = lookup_minimal_symbol (name_got_plt, NULL, objfile);
      if (msym == NULL)
	continue;
      if (MSYMBOL_TYPE (msym) != mst_slot_got_plt)
	continue;
      pointer_address = SYMBOL_VALUE_ADDRESS (msym);

      plt = bfd_get_section_by_name (obfd, ".plt");
      if (plt == NULL)
	continue;

      if (MSYMBOL_SIZE (msym) != ptr_size)
	continue;
      if (target_read_memory (pointer_address, buf, ptr_size) != 0)
	continue;
      addr = extract_typed_address (buf, ptr_type);
      addr = gdbarch_convert_from_func_ptr_addr (gdbarch, addr,
						 &current_target);

      if (addr_p)
	*addr_p = addr;
      if (elf_gnu_ifunc_record_cache (name, addr))
	return 1;
    }

  return 0;
}

/* Try to find the target resolved function entry address of a STT_GNU_IFUNC
   function NAME.  If the address is found it is stored to *ADDR_P (if ADDR_P
   is not NULL) and the function returns 1.  It returns 0 otherwise.

   Both the elf_objfile_gnu_ifunc_cache_data hash table and
   SYMBOL_GOT_PLT_SUFFIX locations are searched by this function.  */

static int
elf_gnu_ifunc_resolve_name (const char *name, CORE_ADDR *addr_p)
{
  if (elf_gnu_ifunc_resolve_by_cache (name, addr_p))
    return 1;

  if (elf_gnu_ifunc_resolve_by_got (name, addr_p))
    return 1;

  return 0;
}

/* Call STT_GNU_IFUNC - a function returning addresss of a real function to
   call.  PC is theSTT_GNU_IFUNC resolving function entry.  The value returned
   is the entry point of the resolved STT_GNU_IFUNC target function to call.
   */

static CORE_ADDR
elf_gnu_ifunc_resolve_addr (struct gdbarch *gdbarch, CORE_ADDR pc)
{
  const char *name_at_pc;
  CORE_ADDR start_at_pc, address;
  struct type *func_func_type = builtin_type (gdbarch)->builtin_func_func;
  struct value *function, *address_val;

  /* Try first any non-intrusive methods without an inferior call.  */

  if (find_pc_partial_function (pc, &name_at_pc, &start_at_pc, NULL)
      && start_at_pc == pc)
    {
      if (elf_gnu_ifunc_resolve_name (name_at_pc, &address))
	return address;
    }
  else
    name_at_pc = NULL;

  function = allocate_value (func_func_type);
  set_value_address (function, pc);

  /* STT_GNU_IFUNC resolver functions have no parameters.  FUNCTION is the
     function entry address.  ADDRESS may be a function descriptor.  */

  address_val = call_function_by_hand (function, 0, NULL);
  address = value_as_address (address_val);
  address = gdbarch_convert_from_func_ptr_addr (gdbarch, address,
						&current_target);

  if (name_at_pc)
    elf_gnu_ifunc_record_cache (name_at_pc, address);

  return address;
}

/* Handle inferior hit of bp_gnu_ifunc_resolver, see its definition.  */

static void
elf_gnu_ifunc_resolver_stop (struct breakpoint *b)
{
  struct breakpoint *b_return;
  struct frame_info *prev_frame = get_prev_frame (get_current_frame ());
  struct frame_id prev_frame_id = get_stack_frame_id (prev_frame);
  CORE_ADDR prev_pc = get_frame_pc (prev_frame);
  int thread_id = pid_to_thread_id (inferior_ptid);

  gdb_assert (b->type == bp_gnu_ifunc_resolver);

  for (b_return = b->related_breakpoint; b_return != b;
       b_return = b_return->related_breakpoint)
    {
      gdb_assert (b_return->type == bp_gnu_ifunc_resolver_return);
      gdb_assert (b_return->loc != NULL && b_return->loc->next == NULL);
      gdb_assert (frame_id_p (b_return->frame_id));

      if (b_return->thread == thread_id
	  && b_return->loc->requested_address == prev_pc
	  && frame_id_eq (b_return->frame_id, prev_frame_id))
	break;
    }

  if (b_return == b)
    {
      struct symtab_and_line sal;

      /* No need to call find_pc_line for symbols resolving as this is only
	 a helper breakpointer never shown to the user.  */

      init_sal (&sal);
      sal.pspace = current_inferior ()->pspace;
      sal.pc = prev_pc;
      sal.section = find_pc_overlay (sal.pc);
      sal.explicit_pc = 1;
      b_return = set_momentary_breakpoint (get_frame_arch (prev_frame), sal,
					   prev_frame_id,
					   bp_gnu_ifunc_resolver_return);

      /* set_momentary_breakpoint invalidates PREV_FRAME.  */
      prev_frame = NULL;

      /* Add new b_return to the ring list b->related_breakpoint.  */
      gdb_assert (b_return->related_breakpoint == b_return);
      b_return->related_breakpoint = b->related_breakpoint;
      b->related_breakpoint = b_return;
    }
}

/* Handle inferior hit of bp_gnu_ifunc_resolver_return, see its definition.  */

static void
elf_gnu_ifunc_resolver_return_stop (struct breakpoint *b)
{
  struct gdbarch *gdbarch = get_frame_arch (get_current_frame ());
  struct type *func_func_type = builtin_type (gdbarch)->builtin_func_func;
  struct type *value_type = TYPE_TARGET_TYPE (func_func_type);
  struct regcache *regcache = get_thread_regcache (inferior_ptid);
  struct value *func_func;
  struct value *value;
  CORE_ADDR resolved_address, resolved_pc;
  struct symtab_and_line sal;
  struct symtabs_and_lines sals, sals_end;

  gdb_assert (b->type == bp_gnu_ifunc_resolver_return);

  while (b->related_breakpoint != b)
    {
      struct breakpoint *b_next = b->related_breakpoint;

      switch (b->type)
	{
	case bp_gnu_ifunc_resolver:
	  break;
	case bp_gnu_ifunc_resolver_return:
	  delete_breakpoint (b);
	  break;
	default:
	  internal_error (__FILE__, __LINE__,
			  _("handle_inferior_event: Invalid "
			    "gnu-indirect-function breakpoint type %d"),
			  (int) b->type);
	}
      b = b_next;
    }
  gdb_assert (b->type == bp_gnu_ifunc_resolver);
  gdb_assert (b->loc->next == NULL);

  func_func = allocate_value (func_func_type);
  set_value_address (func_func, b->loc->related_address);

  value = allocate_value (value_type);
  gdbarch_return_value (gdbarch, func_func, value_type, regcache,
			value_contents_raw (value), NULL);
  resolved_address = value_as_address (value);
  resolved_pc = gdbarch_convert_from_func_ptr_addr (gdbarch,
						    resolved_address,
						    &current_target);

  gdb_assert (current_program_space == b->pspace || b->pspace == NULL);
  elf_gnu_ifunc_record_cache (b->addr_string, resolved_pc);

  sal = find_pc_line (resolved_pc, 0);
  sals.nelts = 1;
  sals.sals = &sal;
  sals_end.nelts = 0;

  b->type = bp_breakpoint;
  update_breakpoint_locations (b, sals, sals_end);
}

#define BUILD_ID_VERBOSE_NONE 0
#define BUILD_ID_VERBOSE_FILENAMES 1
#define BUILD_ID_VERBOSE_BINARY_PARSE 2
static int build_id_verbose = BUILD_ID_VERBOSE_FILENAMES;
static void
show_build_id_verbose (struct ui_file *file, int from_tty,
		       struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file, _("Verbosity level of the build-id locator is %s.\n"),
		    value);
}

/* Locate NT_GNU_BUILD_ID and return its matching debug filename.
   FIXME: NOTE decoding should be unified with the BFD core notes decoding.  */

static struct elf_build_id *
build_id_buf_get (bfd *templ, gdb_byte *buf, bfd_size_type size)
{
  bfd_byte *p;

  p = buf;
  while (p < buf + size)
    {
      /* FIXME: bad alignment assumption.  */
      Elf_External_Note *xnp = (Elf_External_Note *) p;
      size_t namesz = H_GET_32 (templ, xnp->namesz);
      size_t descsz = H_GET_32 (templ, xnp->descsz);
      bfd_byte *descdata = xnp->name + BFD_ALIGN (namesz, 4);

      if (H_GET_32 (templ, xnp->type) == NT_GNU_BUILD_ID
	  && namesz == sizeof "GNU"
	  && memcmp (xnp->name, "GNU", sizeof "GNU") == 0)
	{
	  size_t size = descsz;
	  gdb_byte *data = (void *) descdata;
	  struct elf_build_id *retval;

	  retval = xmalloc (sizeof *retval - 1 + size);
	  retval->size = size;
	  memcpy (retval->data, data, size);

	  return retval;
	}
      p = descdata + BFD_ALIGN (descsz, 4);
    }
  return NULL;
}

/* Separate debuginfo files have corrupted PHDR but SHDR is correct there.
   Locate NT_GNU_BUILD_ID from ABFD and return its content.  */

static const struct elf_build_id *
build_id_bfd_shdr_get (bfd *abfd)
{
  if (!bfd_check_format (abfd, bfd_object)
      || bfd_get_flavour (abfd) != bfd_target_elf_flavour
      || elf_tdata (abfd)->build_id == NULL)
    return NULL;

  return elf_tdata (abfd)->build_id;
}

/* Core files may have missing (corrupt) SHDR but PDHR is correct there.
   bfd_elf_bfd_from_remote_memory () has too much overhead by
   allocating/reading all the available ELF PT_LOADs.  */

static struct elf_build_id *
build_id_phdr_get (bfd *templ, bfd_vma loadbase, unsigned e_phnum,
		   Elf_Internal_Phdr *i_phdr)
{
  int i;
  struct elf_build_id *retval = NULL;

  for (i = 0; i < e_phnum; i++)
    if (i_phdr[i].p_type == PT_NOTE && i_phdr[i].p_filesz > 0)
      {
	Elf_Internal_Phdr *hdr = &i_phdr[i];
	gdb_byte *buf;
	int err;

	buf = xmalloc (hdr->p_filesz);
	err = target_read_memory (loadbase + i_phdr[i].p_vaddr, buf,
				  hdr->p_filesz);
	if (err == 0)
	  retval = build_id_buf_get (templ, buf, hdr->p_filesz);
	else
	  retval = NULL;
	xfree (buf);
	if (retval != NULL)
	  break;
      }
  return retval;
}

/* First we validate the file by reading in the ELF header and checking
   the magic number.  */

static inline bfd_boolean
elf_file_p (Elf64_External_Ehdr *x_ehdrp64)
{
  gdb_assert (sizeof (Elf64_External_Ehdr) >= sizeof (Elf32_External_Ehdr));
  gdb_assert (offsetof (Elf64_External_Ehdr, e_ident)
	      == offsetof (Elf32_External_Ehdr, e_ident));
  gdb_assert (sizeof (((Elf64_External_Ehdr *) 0)->e_ident)
	      == sizeof (((Elf32_External_Ehdr *) 0)->e_ident));

  return ((x_ehdrp64->e_ident[EI_MAG0] == ELFMAG0)
	  && (x_ehdrp64->e_ident[EI_MAG1] == ELFMAG1)
	  && (x_ehdrp64->e_ident[EI_MAG2] == ELFMAG2)
	  && (x_ehdrp64->e_ident[EI_MAG3] == ELFMAG3));
}

/* Translate an ELF file header in external format into an ELF file header in
   internal format.  */

#define H_GET_WORD(bfd, ptr) (is64 ? H_GET_64 (bfd, (ptr))		\
				   : H_GET_32 (bfd, (ptr)))
#define H_GET_SIGNED_WORD(bfd, ptr) (is64 ? H_GET_S64 (bfd, (ptr))	\
					  : H_GET_S32 (bfd, (ptr)))

static void
elf_swap_ehdr_in (bfd *abfd,
		  const Elf64_External_Ehdr *src64,
		  Elf_Internal_Ehdr *dst)
{
  int is64 = bfd_get_arch_size (abfd) == 64;
#define SRC(field) (is64 ? src64->field \
			 : ((const Elf32_External_Ehdr *) src64)->field)

  int signed_vma = get_elf_backend_data (abfd)->sign_extend_vma;
  memcpy (dst->e_ident, SRC (e_ident), EI_NIDENT);
  dst->e_type = H_GET_16 (abfd, SRC (e_type));
  dst->e_machine = H_GET_16 (abfd, SRC (e_machine));
  dst->e_version = H_GET_32 (abfd, SRC (e_version));
  if (signed_vma)
    dst->e_entry = H_GET_SIGNED_WORD (abfd, SRC (e_entry));
  else
    dst->e_entry = H_GET_WORD (abfd, SRC (e_entry));
  dst->e_phoff = H_GET_WORD (abfd, SRC (e_phoff));
  dst->e_shoff = H_GET_WORD (abfd, SRC (e_shoff));
  dst->e_flags = H_GET_32 (abfd, SRC (e_flags));
  dst->e_ehsize = H_GET_16 (abfd, SRC (e_ehsize));
  dst->e_phentsize = H_GET_16 (abfd, SRC (e_phentsize));
  dst->e_phnum = H_GET_16 (abfd, SRC (e_phnum));
  dst->e_shentsize = H_GET_16 (abfd, SRC (e_shentsize));
  dst->e_shnum = H_GET_16 (abfd, SRC (e_shnum));
  dst->e_shstrndx = H_GET_16 (abfd, SRC (e_shstrndx));

#undef SRC
}

/* Translate an ELF program header table entry in external format into an
   ELF program header table entry in internal format.  */

static void
elf_swap_phdr_in (bfd *abfd,
		  const Elf64_External_Phdr *src64,
		  Elf_Internal_Phdr *dst)
{
  int is64 = bfd_get_arch_size (abfd) == 64;
#define SRC(field) (is64 ? src64->field					\
			 : ((const Elf32_External_Phdr *) src64)->field)

  int signed_vma = get_elf_backend_data (abfd)->sign_extend_vma;

  dst->p_type = H_GET_32 (abfd, SRC (p_type));
  dst->p_flags = H_GET_32 (abfd, SRC (p_flags));
  dst->p_offset = H_GET_WORD (abfd, SRC (p_offset));
  if (signed_vma)
    {
      dst->p_vaddr = H_GET_SIGNED_WORD (abfd, SRC (p_vaddr));
      dst->p_paddr = H_GET_SIGNED_WORD (abfd, SRC (p_paddr));
    }
  else
    {
      dst->p_vaddr = H_GET_WORD (abfd, SRC (p_vaddr));
      dst->p_paddr = H_GET_WORD (abfd, SRC (p_paddr));
    }
  dst->p_filesz = H_GET_WORD (abfd, SRC (p_filesz));
  dst->p_memsz = H_GET_WORD (abfd, SRC (p_memsz));
  dst->p_align = H_GET_WORD (abfd, SRC (p_align));

#undef SRC
}

#undef H_GET_SIGNED_WORD
#undef H_GET_WORD

static Elf_Internal_Phdr *
elf_get_phdr (bfd *templ, bfd_vma ehdr_vma, unsigned *e_phnum_pointer,
              bfd_vma *loadbase_pointer)
{
  /* sizeof (Elf64_External_Ehdr) >= sizeof (Elf32_External_Ehdr)  */
  Elf64_External_Ehdr x_ehdr64;	/* Elf file header, external form */
  Elf_Internal_Ehdr i_ehdr;	/* Elf file header, internal form */
  bfd_size_type x_phdrs_size;
  gdb_byte *x_phdrs_ptr;
  Elf_Internal_Phdr *i_phdrs;
  int err;
  unsigned int i;
  bfd_vma loadbase;
  int loadbase_set;

  gdb_assert (templ != NULL);
  gdb_assert (sizeof (Elf64_External_Ehdr) >= sizeof (Elf32_External_Ehdr));

  /* Read in the ELF header in external format.  */
  err = target_read_memory (ehdr_vma, (bfd_byte *) &x_ehdr64, sizeof x_ehdr64);
  if (err)
    {
      if (build_id_verbose >= BUILD_ID_VERBOSE_BINARY_PARSE)
        warning (_("build-id: Error reading ELF header at address 0x%lx"),
		 (unsigned long) ehdr_vma);
      return NULL;
    }

  /* Now check to see if we have a valid ELF file, and one that BFD can
     make use of.  The magic number must match, the address size ('class')
     and byte-swapping must match our XVEC entry.  */

  if (! elf_file_p (&x_ehdr64)
      || x_ehdr64.e_ident[EI_VERSION] != EV_CURRENT
      || !((bfd_get_arch_size (templ) == 64
            && x_ehdr64.e_ident[EI_CLASS] == ELFCLASS64)
           || (bfd_get_arch_size (templ) == 32
	       && x_ehdr64.e_ident[EI_CLASS] == ELFCLASS32)))
    {
      if (build_id_verbose >= BUILD_ID_VERBOSE_BINARY_PARSE)
        warning (_("build-id: Unrecognized ELF header at address 0x%lx"),
		 (unsigned long) ehdr_vma);
      return NULL;
    }

  /* Check that file's byte order matches xvec's */
  switch (x_ehdr64.e_ident[EI_DATA])
    {
    case ELFDATA2MSB:		/* Big-endian */
      if (! bfd_header_big_endian (templ))
	{
	  if (build_id_verbose >= BUILD_ID_VERBOSE_BINARY_PARSE)
	    warning (_("build-id: Unrecognized "
		       "big-endian ELF header at address 0x%lx"),
		     (unsigned long) ehdr_vma);
	  return NULL;
	}
      break;
    case ELFDATA2LSB:		/* Little-endian */
      if (! bfd_header_little_endian (templ))
	{
	  if (build_id_verbose >= BUILD_ID_VERBOSE_BINARY_PARSE)
	    warning (_("build-id: Unrecognized "
		       "little-endian ELF header at address 0x%lx"),
		     (unsigned long) ehdr_vma);
	  return NULL;
	}
      break;
    case ELFDATANONE:		/* No data encoding specified */
    default:			/* Unknown data encoding specified */
      if (build_id_verbose >= BUILD_ID_VERBOSE_BINARY_PARSE)
	warning (_("build-id: Unrecognized "
		   "ELF header endianity at address 0x%lx"),
		 (unsigned long) ehdr_vma);
      return NULL;
    }

  elf_swap_ehdr_in (templ, &x_ehdr64, &i_ehdr);

  /* The file header tells where to find the program headers.
     These are what we use to actually choose what to read.  */

  if (i_ehdr.e_phentsize != (bfd_get_arch_size (templ) == 64
                             ? sizeof (Elf64_External_Phdr)
			     : sizeof (Elf32_External_Phdr))
      || i_ehdr.e_phnum == 0)
    {
      if (build_id_verbose >= BUILD_ID_VERBOSE_BINARY_PARSE)
	warning (_("build-id: Invalid ELF program headers from the ELF header "
		   "at address 0x%lx"), (unsigned long) ehdr_vma);
      return NULL;
    }

  x_phdrs_size = (bfd_get_arch_size (templ) == 64 ? sizeof (Elf64_External_Phdr)
						: sizeof (Elf32_External_Phdr));

  i_phdrs = xmalloc (i_ehdr.e_phnum * (sizeof *i_phdrs + x_phdrs_size));
  x_phdrs_ptr = (void *) &i_phdrs[i_ehdr.e_phnum];
  err = target_read_memory (ehdr_vma + i_ehdr.e_phoff, (bfd_byte *) x_phdrs_ptr,
			    i_ehdr.e_phnum * x_phdrs_size);
  if (err)
    {
      free (i_phdrs);
      if (build_id_verbose >= BUILD_ID_VERBOSE_BINARY_PARSE)
        warning (_("build-id: Error reading "
		   "ELF program headers at address 0x%lx"),
		 (unsigned long) (ehdr_vma + i_ehdr.e_phoff));
      return NULL;
    }

  loadbase = ehdr_vma;
  loadbase_set = 0;
  for (i = 0; i < i_ehdr.e_phnum; ++i)
    {
      elf_swap_phdr_in (templ, (Elf64_External_Phdr *)
			       (x_phdrs_ptr + i * x_phdrs_size), &i_phdrs[i]);
      /* IA-64 vDSO may have two mappings for one segment, where one mapping
	 is executable only, and one is read only.  We must not use the
	 executable one (PF_R is the first one, PF_X the second one).  */
      if (i_phdrs[i].p_type == PT_LOAD && (i_phdrs[i].p_flags & PF_R))
	{
	  /* Only the first PT_LOAD segment indicates the file bias.
	     Next segments may have P_VADDR arbitrarily higher.
	     If the first segment has P_VADDR zero any next segment must not
	     confuse us, the first one sets LOADBASE certainly enough.  */
	  if (!loadbase_set && i_phdrs[i].p_offset == 0)
	    {
	      loadbase = ehdr_vma - i_phdrs[i].p_vaddr;
	      loadbase_set = 1;
	    }
	}
    }

  if (build_id_verbose >= BUILD_ID_VERBOSE_BINARY_PARSE)
    warning (_("build-id: Found ELF header at address 0x%lx, loadbase 0x%lx"),
	     (unsigned long) ehdr_vma, (unsigned long) loadbase);

  *e_phnum_pointer = i_ehdr.e_phnum;
  *loadbase_pointer = loadbase;
  return i_phdrs;
}

/* BUILD_ID_ADDR_GET gets ADDR located somewhere in the object.
   Find the first section before ADDR containing an ELF header.
   We rely on the fact the sections from multiple files do not mix.
   FIXME: We should check ADDR is contained _inside_ the section with possibly
   missing content (P_FILESZ < P_MEMSZ).  These omitted sections are currently
   hidden by _BFD_ELF_MAKE_SECTION_FROM_PHDR.  */

static CORE_ADDR build_id_addr;
struct build_id_addr_sect
  {
    struct build_id_addr_sect *next;
    asection *sect;
  };
static struct build_id_addr_sect *build_id_addr_sect;

static void build_id_addr_candidate (bfd *abfd, asection *sect, void *obj)
{
  if (build_id_addr >= bfd_section_vma (abfd, sect))
    {
      struct build_id_addr_sect *candidate;

      candidate = xmalloc (sizeof *candidate);
      candidate->next = build_id_addr_sect;
      build_id_addr_sect = candidate;
      candidate->sect = sect;
    }
}

struct elf_build_id *
build_id_addr_get (CORE_ADDR addr)
{
  struct build_id_addr_sect *candidate;
  struct elf_build_id *retval = NULL;
  Elf_Internal_Phdr *i_phdr = NULL;
  bfd_vma loadbase = 0;
  unsigned e_phnum = 0;

  if (core_bfd == NULL)
    return NULL;

  build_id_addr = addr;
  gdb_assert (build_id_addr_sect == NULL);
  bfd_map_over_sections (core_bfd, build_id_addr_candidate, NULL);

  /* Sections are sorted in the high-to-low VMAs order.
     Stop the search on the first ELF header we find.
     Do not continue the search even if it does not contain NT_GNU_BUILD_ID.  */

  for (candidate = build_id_addr_sect; candidate != NULL;
       candidate = candidate->next)
    {
      i_phdr = elf_get_phdr (core_bfd,
			     bfd_section_vma (core_bfd, candidate->sect),
			     &e_phnum, &loadbase);
      if (i_phdr != NULL)
	break;
    }

  if (i_phdr != NULL)
    {
      retval = build_id_phdr_get (core_bfd, loadbase, e_phnum, i_phdr);
      xfree (i_phdr);
    }

  while (build_id_addr_sect != NULL)
    {
      candidate = build_id_addr_sect;
      build_id_addr_sect = candidate->next;
      xfree (candidate);
    }

  return retval;
}

/* Return if FILENAME has NT_GNU_BUILD_ID matching the CHECK value.  */

static int
build_id_verify (const char *filename, const struct elf_build_id *check)
{
  bfd *abfd;
  const struct elf_build_id *found;
  int retval = 0;

  /* We expect to be silent on the non-existing files.  */
  abfd = gdb_bfd_open_maybe_remote (filename);
  if (abfd == NULL)
    return 0;

  found = build_id_bfd_shdr_get (abfd);

  if (found == NULL)
    warning (_("File \"%s\" has no build-id, file skipped"), filename);
  else if (found->size != check->size
           || memcmp (found->data, check->data, found->size) != 0)
    warning (_("File \"%s\" has a different build-id, file skipped"),
	     filename);
  else
    retval = 1;

  gdb_bfd_unref (abfd);

  return retval;
}

static char *
link_resolve (const char *symlink, int level)
{
  char buf[PATH_MAX + 1], *target, *retval;
  ssize_t got;

  if (level > 10)
    return xstrdup (symlink);

  got = readlink (symlink, buf, sizeof (buf));
  if (got < 0 || got >= sizeof (buf))
    return xstrdup (symlink);
  buf[got] = '\0';

  if (IS_ABSOLUTE_PATH (buf))
    target = xstrdup (buf);
  else
    {
      char *dir = ldirname (symlink);

      if (dir == NULL)
	return xstrdup (symlink);
      target = xstrprintf ("%s"
#ifndef HAVE_DOS_BASED_FILE_SYSTEM
			   "/"
#else /* HAVE_DOS_BASED_FILE_SYSTEM */
			   "\\"
#endif /* HAVE_DOS_BASED_FILE_SYSTEM */
			   "%s", dir, buf);
    }

  retval = link_resolve (target, level + 1);
  xfree (target);
  return retval;
}

char *
build_id_to_filename (const struct elf_build_id *build_id, char **link_return,
		      int add_debug_suffix)
{
  char *link, *debugdir, *retval = NULL;
  char *link_all = NULL;
  VEC (char_ptr) *debugdir_vec;
  struct cleanup *back_to;
  int ix;

  /* DEBUG_FILE_DIRECTORY/.build-id/ab/cdef */
  link = xmalloc (strlen (debug_file_directory) + 2 * build_id->size + 50);

  /* Keep backward compatibility so that DEBUG_FILE_DIRECTORY being "" will
     cause "/.build-id/..." lookups.  */

  debugdir_vec = dirnames_to_char_ptr_vec (debug_file_directory);
  back_to = make_cleanup_free_char_ptr_vec (debugdir_vec);

  for (ix = 0; VEC_iterate (char_ptr, debugdir_vec, ix, debugdir); ++ix)
    {
      size_t debugdir_len = strlen (debugdir);
      const gdb_byte *data = build_id->data;
      size_t size = build_id->size;
      unsigned seqno;
      struct stat statbuf_trash;
      /* Initialize it just to avoid a GCC false warning.  */
      char *s, *link0 = NULL, *link0_resolved;

      memcpy (link, debugdir, debugdir_len);
      s = &link[debugdir_len];
      s += sprintf (s, "/.build-id/");
      if (size > 0)
	{
	  size--;
	  s += sprintf (s, "%02x", (unsigned) *data++);
	}
      if (size > 0)
	*s++ = '/';
      while (size-- > 0)
	s += sprintf (s, "%02x", (unsigned) *data++);

      for (seqno = 0;; seqno++)
	{
	  char *s2;

	  if (seqno)
	    {
	      /* There can be multiple build-id symlinks pointing to real files
		 with the same build-id (such as hard links).  Some of the real
		 files may not be installed.  */

	      s2 = s + sprintf (s, ".%u", seqno);
	    }
	  else
	    s2 = s;

	  if (add_debug_suffix)
	    strcpy (s2, ".debug");
	  else
	    *s2 = 0;

	  if (!seqno)
	    {
	      /* If none of the real files is found report as missing file
		 always the non-.%u-suffixed file.  */
	      link0 = xstrdup (link);
	    }

	  /* `access' automatically dereferences LINK.  */
	  if (lstat (link, &statbuf_trash) != 0)
	    {
	      /* Stop increasing SEQNO.  */
	      break;
	    }

	  retval = lrealpath (link);

	  if (retval != NULL && !build_id_verify (retval, build_id))
	    {
	      xfree (retval);
	      retval = NULL;
	    }

	  if (retval)
	    break;
	}

      if (retval != NULL)
	{
	  /* LINK_ALL is not used below in this non-NULL RETVAL case.  */
	  xfree (link0);
	  break;
	}

      /* If the symlink has target request to install the target.
         BASE-debuginfo.rpm contains the symlink but BASE.rpm may be missing.
         https://bugzilla.redhat.com/show_bug.cgi?id=981154  */
      link0_resolved = link_resolve (link0, 0);
      xfree (link0);

      if (link_all == NULL)
	link_all = xstrdup (link0_resolved);
      else
	{
	  size_t len_orig = strlen (link_all);

	  link_all = xrealloc (link_all,
			       len_orig + 1 + strlen (link0_resolved) + 1);

	  /* Use whitespace instead of DIRNAME_SEPARATOR to be compatible with
	     its possible use as an argument for installation command.  */
	  link_all[len_orig] = ' ';

	  strcpy (&link_all[len_orig + 1], link0_resolved);
	}
      xfree (link0_resolved);
    }

  if (link_return != NULL)
    {
      if (retval != NULL)
	{
	  *link_return = link;
	  link = NULL;
	}
      else
	{
	  *link_return = link_all;
	  link_all = NULL;
	}
    }
  xfree (link);
  xfree (link_all);

  do_cleanups (back_to);
  return retval;
}

#ifdef HAVE_LIBRPM

#include <rpm/rpmlib.h>
#include <rpm/rpmts.h>
#include <rpm/rpmdb.h>
#include <rpm/header.h>
#ifdef DLOPEN_LIBRPM
#include <dlfcn.h>
#endif

/* Workarodun https://bugzilla.redhat.com/show_bug.cgi?id=643031
   librpm must not exit() an application on SIGINT

   Enable or disable a signal handler.  SIGNUM: signal to enable (or disable
   if negative).  HANDLER: sa_sigaction handler (or NULL to use
   rpmsqHandler()).  Returns: no. of refs, -1 on error.  */
extern int rpmsqEnable (int signum, /* rpmsqAction_t handler */ void *handler);
int
rpmsqEnable (int signum, /* rpmsqAction_t handler */ void *handler)
{
  return 0;
}

/* This MISSING_RPM_HASH tracker is used to collect all the missing rpm files
   and avoid their duplicities during a single inferior run.  */

static struct htab *missing_rpm_hash;

/* This MISSING_RPM_LIST tracker is used to collect and print as a single line
   all the rpms right before the nearest GDB prompt.  It gets cleared after
   each such print (it is questionable if we should clear it after the print).
   */

struct missing_rpm
  {
    struct missing_rpm *next;
    char rpm[1];
  };
static struct missing_rpm *missing_rpm_list;
static int missing_rpm_list_entries;

/* Returns the count of newly added rpms.  */

static int
missing_rpm_enlist (const char *filename)
{
  static int rpm_init_done = 0;
  rpmts ts;
  rpmdbMatchIterator mi;
  int count = 0;

#ifdef DLOPEN_LIBRPM
  /* Duplicate here the declarations to verify they match.  The same sanity
     check is present also in `configure.ac'.  */
  extern char * headerFormat(Header h, const char * fmt, errmsg_t * errmsg);
  static char *(*headerFormat_p) (Header h, const char * fmt, errmsg_t *errmsg);
  extern int rpmReadConfigFiles(const char * file, const char * target);
  static int (*rpmReadConfigFiles_p) (const char * file, const char * target);
  extern rpmdbMatchIterator rpmdbFreeIterator(rpmdbMatchIterator mi);
  static rpmdbMatchIterator (*rpmdbFreeIterator_p) (rpmdbMatchIterator mi);
  extern Header rpmdbNextIterator(rpmdbMatchIterator mi);
  static Header (*rpmdbNextIterator_p) (rpmdbMatchIterator mi);
  extern rpmts rpmtsCreate(void);
  static rpmts (*rpmtsCreate_p) (void);
  extern rpmts rpmtsFree(rpmts ts);
  static rpmts (*rpmtsFree_p) (rpmts ts);
  extern rpmdbMatchIterator rpmtsInitIterator(const rpmts ts, rpmTag rpmtag,
                                              const void * keyp, size_t keylen);
  static rpmdbMatchIterator (*rpmtsInitIterator_p) (const rpmts ts,
						    rpmTag rpmtag,
						    const void *keyp,
						    size_t keylen);
#else	/* !DLOPEN_LIBRPM */
# define headerFormat_p headerFormat
# define rpmReadConfigFiles_p rpmReadConfigFiles
# define rpmdbFreeIterator_p rpmdbFreeIterator
# define rpmdbNextIterator_p rpmdbNextIterator
# define rpmtsCreate_p rpmtsCreate
# define rpmtsFree_p rpmtsFree
# define rpmtsInitIterator_p rpmtsInitIterator
#endif	/* !DLOPEN_LIBRPM */

  gdb_assert (filename != NULL);

  if (strcmp (filename, BUILD_ID_MAIN_EXECUTABLE_FILENAME) == 0)
    return 0;

  if (filename[0] != '/')
    {
      warning (_("Ignoring non-absolute filename: <%s>"), filename);
      return 0;
    }

  if (!rpm_init_done)
    {
      static int init_tried;

      /* Already failed the initialization before?  */
      if (init_tried)
      	return 0;
      init_tried = 1;

#ifdef DLOPEN_LIBRPM
      {
	void *h;

	h = dlopen (DLOPEN_LIBRPM, RTLD_LAZY);
	if (!h)
	  {
	    warning (_("Unable to open \"%s\" (%s), "
		      "missing debuginfos notifications will not be displayed"),
		     DLOPEN_LIBRPM, dlerror ());
	    return 0;
	  }

	if (!((headerFormat_p = dlsym (h, "headerFormat"))
	      && (rpmReadConfigFiles_p = dlsym (h, "rpmReadConfigFiles"))
	      && (rpmdbFreeIterator_p = dlsym (h, "rpmdbFreeIterator"))
	      && (rpmdbNextIterator_p = dlsym (h, "rpmdbNextIterator"))
	      && (rpmtsCreate_p = dlsym (h, "rpmtsCreate"))
	      && (rpmtsFree_p = dlsym (h, "rpmtsFree"))
	      && (rpmtsInitIterator_p = dlsym (h, "rpmtsInitIterator"))))
	  {
	    warning (_("Opened library \"%s\" is incompatible (%s), "
		      "missing debuginfos notifications will not be displayed"),
		     DLOPEN_LIBRPM, dlerror ());
	    if (dlclose (h))
	      warning (_("Error closing library \"%s\": %s\n"), DLOPEN_LIBRPM,
		       dlerror ());
	    return 0;
	  }
      }
#endif	/* DLOPEN_LIBRPM */

      if (rpmReadConfigFiles_p (NULL, NULL) != 0)
	{
	  warning (_("Error reading the rpm configuration files"));
	  return 0;
	}

      rpm_init_done = 1;
    }

  ts = rpmtsCreate_p ();

  mi = rpmtsInitIterator_p (ts, RPMTAG_BASENAMES, filename, 0);
  if (mi != NULL)
    {
      for (;;)
	{
	  Header h;
	  char *debuginfo, **slot, *s, *s2;
	  errmsg_t err;
	  size_t srcrpmlen = sizeof (".src.rpm") - 1;
	  size_t debuginfolen = sizeof ("-debuginfo") - 1;
	  rpmdbMatchIterator mi_debuginfo;

	  h = rpmdbNextIterator_p (mi);
	  if (h == NULL)
	    break;

	  /* Verify the debuginfo file is not already installed.  */

	  debuginfo = headerFormat_p (h, "%{sourcerpm}-debuginfo.%{arch}",
				      &err);
	  if (!debuginfo)
	    {
	      warning (_("Error querying the rpm file `%s': %s"), filename,
	               err);
	      continue;
	    }
	  /* s = `.src.rpm-debuginfo.%{arch}' */
	  s = strrchr (debuginfo, '-') - srcrpmlen;
	  s2 = NULL;
	  if (s > debuginfo && memcmp (s, ".src.rpm", srcrpmlen) == 0)
	    {
	      /* s2 = `-%{release}.src.rpm-debuginfo.%{arch}' */
	      s2 = memrchr (debuginfo, '-', s - debuginfo);
	    }
	  if (s2)
	    {
	      /* s2 = `-%{version}-%{release}.src.rpm-debuginfo.%{arch}' */
	      s2 = memrchr (debuginfo, '-', s2 - debuginfo);
	    }
	  if (!s2)
	    {
	      warning (_("Error querying the rpm file `%s': %s"), filename,
	               debuginfo);
	      xfree (debuginfo);
	      continue;
	    }
	  /* s = `.src.rpm-debuginfo.%{arch}' */
	  /* s2 = `-%{version}-%{release}.src.rpm-debuginfo.%{arch}' */
	  memmove (s2 + debuginfolen, s2, s - s2);
	  memcpy (s2, "-debuginfo", debuginfolen);
	  /* s = `XXXX.%{arch}' */
	  /* strlen ("XXXX") == srcrpmlen + debuginfolen */
	  /* s2 = `-debuginfo-%{version}-%{release}XX.%{arch}' */
	  /* strlen ("XX") == srcrpmlen */
	  memmove (s + debuginfolen, s + srcrpmlen + debuginfolen,
		   strlen (s + srcrpmlen + debuginfolen) + 1);
	  /* s = `-debuginfo-%{version}-%{release}.%{arch}' */

	  /* RPMDBI_PACKAGES requires keylen == sizeof (int).  */
	  /* RPMDBI_LABEL is an interface for NVR-based dbiFindByLabel().  */
	  mi_debuginfo = rpmtsInitIterator_p (ts, RPMDBI_LABEL, debuginfo, 0);
	  xfree (debuginfo);
	  if (mi_debuginfo)
	    {
	      rpmdbFreeIterator_p (mi_debuginfo);
	      count = 0;
	      break;
	    }

	  /* The allocated memory gets utilized below for MISSING_RPM_HASH.  */
	  debuginfo = headerFormat_p (h,
				      "%{name}-%{version}-%{release}.%{arch}",
				      &err);
	  if (!debuginfo)
	    {
	      warning (_("Error querying the rpm file `%s': %s"), filename,
	               err);
	      continue;
	    }

	  /* Base package name for `debuginfo-install'.  We do not use the
	     `yum' command directly as the line
		 yum --enablerepo='*debug*' install NAME-debuginfo.ARCH
	     would be more complicated than just:
		 debuginfo-install NAME-VERSION-RELEASE.ARCH
	     Do not supply the rpm base name (derived from .src.rpm name) as
	     debuginfo-install is unable to install the debuginfo package if
	     the base name PKG binary rpm is not installed while for example
	     PKG-libs would be installed (RH Bug 467901).
	     FUTURE: After multiple debuginfo versions simultaneously installed
	     get supported the support for the VERSION-RELEASE tags handling
	     may need an update.  */

	  if (missing_rpm_hash == NULL)
	    {
	      /* DEL_F is passed NULL as MISSING_RPM_LIST's HTAB_DELETE
		 should not deallocate the entries.  */

	      missing_rpm_hash = htab_create_alloc (64, htab_hash_string,
			       (int (*) (const void *, const void *)) streq,
						    NULL, xcalloc, xfree);
	    }
	  slot = (char **) htab_find_slot (missing_rpm_hash, debuginfo, INSERT);
	  /* XCALLOC never returns NULL.  */
	  gdb_assert (slot != NULL);
	  if (*slot == NULL)
	    {
	      struct missing_rpm *missing_rpm;

	      *slot = debuginfo;

	      missing_rpm = xmalloc (sizeof (*missing_rpm) + strlen (debuginfo));
	      strcpy (missing_rpm->rpm, debuginfo);
	      missing_rpm->next = missing_rpm_list;
	      missing_rpm_list = missing_rpm;
	      missing_rpm_list_entries++;
	    }
	  else
	    xfree (debuginfo);
	  count++;
	}

      rpmdbFreeIterator_p (mi);
    }

  rpmtsFree_p (ts);

  return count;
}

static int
missing_rpm_list_compar (const char *const *ap, const char *const *bp)
{
  return strcoll (*ap, *bp);
}

/* It returns a NULL-terminated array of strings needing to be FREEd.  It may
   also return only NULL.  */

static void
missing_rpm_list_print (void)
{
  char **array, **array_iter;
  struct missing_rpm *list_iter;
  struct cleanup *cleanups;

  if (missing_rpm_list_entries == 0)
    return;

  array = xmalloc (sizeof (*array) * missing_rpm_list_entries);
  cleanups = make_cleanup (xfree, array);

  array_iter = array;
  for (list_iter = missing_rpm_list; list_iter != NULL;
       list_iter = list_iter->next)
    {
      *array_iter++ = list_iter->rpm;
    }
  gdb_assert (array_iter == array + missing_rpm_list_entries);

  qsort (array, missing_rpm_list_entries, sizeof (*array),
	 (int (*) (const void *, const void *)) missing_rpm_list_compar);

  printf_unfiltered (_("Missing separate debuginfos, use: %s"),
		     "debuginfo-install");
  for (array_iter = array; array_iter < array + missing_rpm_list_entries;
       array_iter++)
    {
      putchar_unfiltered (' ');
      puts_unfiltered (*array_iter);
    }
  putchar_unfiltered ('\n');

  while (missing_rpm_list != NULL)
    {
      list_iter = missing_rpm_list;
      missing_rpm_list = list_iter->next;
      xfree (list_iter);
    }
  missing_rpm_list_entries = 0;

  do_cleanups (cleanups);
}

static void
missing_rpm_change (void)
{
  debug_flush_missing ();

  gdb_assert (missing_rpm_list == NULL);
  if (missing_rpm_hash != NULL)
    {
      htab_delete (missing_rpm_hash);
      missing_rpm_hash = NULL;
    }
}

enum missing_exec
  {
    /* Init state.  EXEC_BFD also still could be NULL.  */
    MISSING_EXEC_NOT_TRIED,
    /* We saw a non-NULL EXEC_BFD but RPM has no info about it.  */
    MISSING_EXEC_NOT_FOUND,
    /* We found EXEC_BFD by RPM and we either have its symbols (either embedded
       or separate) or the main executable's RPM is now contained in
       MISSING_RPM_HASH.  */
    MISSING_EXEC_ENLISTED
  };
static enum missing_exec missing_exec = MISSING_EXEC_NOT_TRIED;

#endif	/* HAVE_LIBRPM */

void
debug_flush_missing (void)
{
#ifdef HAVE_LIBRPM
  missing_rpm_list_print ();
#endif
}

/* This MISSING_FILEPAIR_HASH tracker is used only for the duplicite messages
     yum --enablerepo='*debug*' install ...
   avoidance.  */

struct missing_filepair
  {
    char *binary;
    char *debug;
    char data[1];
  };

static struct htab *missing_filepair_hash;
static struct obstack missing_filepair_obstack;

static void *
missing_filepair_xcalloc (size_t nmemb, size_t nmemb_size)
{
  void *retval;
  size_t size = nmemb * nmemb_size;

  retval = obstack_alloc (&missing_filepair_obstack, size);
  memset (retval, 0, size);
  return retval;
}

static hashval_t
missing_filepair_hash_func (const struct missing_filepair *elem)
{
  hashval_t retval = 0;

  retval ^= htab_hash_string (elem->binary);
  if (elem->debug != NULL)
    retval ^= htab_hash_string (elem->debug);

  return retval;
}

static int
missing_filepair_eq (const struct missing_filepair *elem1,
		       const struct missing_filepair *elem2)
{
  return strcmp (elem1->binary, elem2->binary) == 0
         && ((elem1->debug == NULL) == (elem2->debug == NULL))
         && (elem1->debug == NULL || strcmp (elem1->debug, elem2->debug) == 0);
}

static void
missing_filepair_change (void)
{
  if (missing_filepair_hash != NULL)
    {
      obstack_free (&missing_filepair_obstack, NULL);
      /* All their memory came just from missing_filepair_OBSTACK.  */
      missing_filepair_hash = NULL;
    }
#ifdef HAVE_LIBRPM
  missing_exec = MISSING_EXEC_NOT_TRIED;
#endif
}

static void
debug_print_executable_changed (void)
{
#ifdef HAVE_LIBRPM
  missing_rpm_change ();
#endif
  missing_filepair_change ();
}

/* Notify user the file BINARY with (possibly NULL) associated separate debug
   information file DEBUG is missing.  DEBUG may or may not be the build-id
   file such as would be:
     /usr/lib/debug/.build-id/dd/b1d2ce632721c47bb9e8679f369e2295ce71be.debug
   */

void
debug_print_missing (const char *binary, const char *debug)
{
  size_t binary_len0 = strlen (binary) + 1;
  size_t debug_len0 = debug ? strlen (debug) + 1 : 0;
  struct missing_filepair missing_filepair_find;
  struct missing_filepair *missing_filepair;
  struct missing_filepair **slot;

  if (build_id_verbose < BUILD_ID_VERBOSE_FILENAMES)
    return;

  if (missing_filepair_hash == NULL)
    {
      obstack_init (&missing_filepair_obstack);
      missing_filepair_hash = htab_create_alloc (64,
	(hashval_t (*) (const void *)) missing_filepair_hash_func,
	(int (*) (const void *, const void *)) missing_filepair_eq, NULL,
	missing_filepair_xcalloc, NULL);
    }

  /* Use MISSING_FILEPAIR_FIND first instead of calling obstack_alloc with
     obstack_free in the case of a (rare) match.  The problem is ALLOC_F for
     MISSING_FILEPAIR_HASH allocates from MISSING_FILEPAIR_OBSTACK maintenance
     structures for MISSING_FILEPAIR_HASH.  Calling obstack_free would possibly
     not to free only MISSING_FILEPAIR but also some such structures (allocated
     during the htab_find_slot call).  */

  missing_filepair_find.binary = (char *) binary;
  missing_filepair_find.debug = (char *) debug;
  slot = (struct missing_filepair **) htab_find_slot (missing_filepair_hash,
						      &missing_filepair_find,
						      INSERT);

  /* While it may be still printed duplicitely with the missing debuginfo file
   * it is due to once printing about the binary file build-id link and once
   * about the .debug file build-id link as both the build-id symlinks are
   * located in the debuginfo package.  */

  if (*slot != NULL)
    return;

  missing_filepair = obstack_alloc (&missing_filepair_obstack,
				      sizeof (*missing_filepair) - 1
				      + binary_len0 + debug_len0);
  missing_filepair->binary = missing_filepair->data;
  memcpy (missing_filepair->binary, binary, binary_len0);
  if (debug != NULL)
    {
      missing_filepair->debug = missing_filepair->binary + binary_len0;
      memcpy (missing_filepair->debug, debug, debug_len0);
    }
  else
    missing_filepair->debug = NULL;

  *slot = missing_filepair;

#ifdef HAVE_LIBRPM
  if (missing_exec == MISSING_EXEC_NOT_TRIED)
    {
      char *execfilename;

      execfilename = get_exec_file (0);
      if (execfilename != NULL)
	{
	  if (missing_rpm_enlist (execfilename) == 0)
	    missing_exec = MISSING_EXEC_NOT_FOUND;
	  else
	    missing_exec = MISSING_EXEC_ENLISTED;
	}
    }
  if (missing_exec != MISSING_EXEC_ENLISTED)
    if ((binary[0] == 0 || missing_rpm_enlist (binary) == 0)
	&& (debug == NULL || missing_rpm_enlist (debug) == 0))
#endif	/* HAVE_LIBRPM */
      {
	/* We do not collect and flush these messages as each such message
	   already requires its own separate lines.  */

	fprintf_unfiltered (gdb_stdlog,
			    _("Missing separate debuginfo for %s\n"), binary);
        if (debug != NULL)
	  fprintf_unfiltered (gdb_stdlog, _("Try: %s %s\n"),
			      "yum --enablerepo='*debug*' install", debug);
      }
}

static char *
find_separate_debug_file_by_buildid (struct objfile *objfile,
				     char **build_id_filename_return)
{
  const struct elf_build_id *build_id;

  if (build_id_filename_return)
    *build_id_filename_return = NULL;

  build_id = build_id_bfd_shdr_get (objfile->obfd);
  if (build_id != NULL)
    {
      char *build_id_name;

      build_id_name = build_id_to_filename (build_id, build_id_filename_return,
					    1);
      /* Prevent looping on a stripped .debug file.  */
      if (build_id_name != NULL
	  && filename_cmp (build_id_name, objfile->name) == 0)
        {
	  warning (_("\"%s\": separate debug info file has no debug info"),
		   build_id_name);
	  xfree (build_id_name);
	}
      else if (build_id_name != NULL)
	return build_id_name;
    }
  return NULL;
}

/* Scan and build partial symbols for a symbol file.
   We have been initialized by a call to elf_symfile_init, which
   currently does nothing.

   SECTION_OFFSETS is a set of offsets to apply to relocate the symbols
   in each section.  We simplify it down to a single offset for all
   symbols.  FIXME.

   This function only does the minimum work necessary for letting the
   user "name" things symbolically; it does not read the entire symtab.
   Instead, it reads the external and static symbols and puts them in partial
   symbol tables.  When more extensive information is requested of a
   file, the corresponding partial symbol table is mutated into a full
   fledged symbol table by going back and reading the symbols
   for real.

   We look for sections with specific names, to tell us what debug
   format to look for:  FIXME!!!

   elfstab_build_psymtabs() handles STABS symbols;
   mdebug_build_psymtabs() handles ECOFF debugging information.

   Note that ELF files have a "minimal" symbol table, which looks a lot
   like a COFF symbol table, but has only the minimal information necessary
   for linking.  We process this also, and use the information to
   build gdb's minimal symbol table.  This gives us some minimal debugging
   capability even for files compiled without -g.  */

static void
elf_symfile_read (struct objfile *objfile, int symfile_flags)
{
  bfd *synth_abfd, *abfd = objfile->obfd;
  struct elfinfo ei;
  struct cleanup *back_to;
  long symcount = 0, dynsymcount = 0, synthcount, storage_needed;
  asymbol **symbol_table = NULL, **dyn_symbol_table = NULL;
  asymbol *synthsyms;
  struct dbx_symfile_info *dbx;

  if (symtab_create_debug)
    {
      fprintf_unfiltered (gdb_stdlog,
			  "Reading minimal symbols of objfile %s ...\n",
			  objfile->name);
    }

  init_minimal_symbol_collection ();
  back_to = make_cleanup_discard_minimal_symbols ();

  memset ((char *) &ei, 0, sizeof (ei));

  /* Allocate struct to keep track of the symfile.  */
  dbx = XCNEW (struct dbx_symfile_info);
  set_objfile_data (objfile, dbx_objfile_data_key, dbx);
  make_cleanup (free_elfinfo, (void *) objfile);

  /* Process the normal ELF symbol table first.  This may write some
     chain of info into the dbx_symfile_info of the objfile, which can
     later be used by elfstab_offset_sections.  */

  storage_needed = bfd_get_symtab_upper_bound (objfile->obfd);
  if (storage_needed < 0)
    error (_("Can't read symbols from %s: %s"),
	   bfd_get_filename (objfile->obfd),
	   bfd_errmsg (bfd_get_error ()));

  if (storage_needed > 0)
    {
      /* Memory gets permanently referenced from ABFD after
	 bfd_canonicalize_symtab so it must not get freed before ABFD gets.  */

      symbol_table = bfd_alloc (abfd, storage_needed);
      symcount = bfd_canonicalize_symtab (objfile->obfd, symbol_table);

      if (symcount < 0)
	error (_("Can't read symbols from %s: %s"),
	       bfd_get_filename (objfile->obfd),
	       bfd_errmsg (bfd_get_error ()));

      elf_symtab_read (objfile, ST_REGULAR, symcount, symbol_table, 0);
    }

  /* Add the dynamic symbols.  */

  storage_needed = bfd_get_dynamic_symtab_upper_bound (objfile->obfd);

  if (storage_needed > 0)
    {
      /* Memory gets permanently referenced from ABFD after
	 bfd_get_synthetic_symtab so it must not get freed before ABFD gets.
	 It happens only in the case when elf_slurp_reloc_table sees
	 asection->relocation NULL.  Determining which section is asection is
	 done by _bfd_elf_get_synthetic_symtab which is all a bfd
	 implementation detail, though.  */

      dyn_symbol_table = bfd_alloc (abfd, storage_needed);
      dynsymcount = bfd_canonicalize_dynamic_symtab (objfile->obfd,
						     dyn_symbol_table);

      if (dynsymcount < 0)
	error (_("Can't read symbols from %s: %s"),
	       bfd_get_filename (objfile->obfd),
	       bfd_errmsg (bfd_get_error ()));

      elf_symtab_read (objfile, ST_DYNAMIC, dynsymcount, dyn_symbol_table, 0);

      elf_rel_plt_read (objfile, dyn_symbol_table);
    }

  /* Contrary to binutils --strip-debug/--only-keep-debug the strip command from
     elfutils (eu-strip) moves even the .symtab section into the .debug file.

     bfd_get_synthetic_symtab on ppc64 for each function descriptor ELF symbol
     'name' creates a new BSF_SYNTHETIC ELF symbol '.name' with its code
     address.  But with eu-strip files bfd_get_synthetic_symtab would fail to
     read the code address from .opd while it reads the .symtab section from
     a separate debug info file as the .opd section is SHT_NOBITS there.

     With SYNTH_ABFD the .opd section will be read from the original
     backlinked binary where it is valid.  */

  if (objfile->separate_debug_objfile_backlink)
    synth_abfd = objfile->separate_debug_objfile_backlink->obfd;
  else
    synth_abfd = abfd;

  /* Add synthetic symbols - for instance, names for any PLT entries.  */

  synthcount = bfd_get_synthetic_symtab (synth_abfd, symcount, symbol_table,
					 dynsymcount, dyn_symbol_table,
					 &synthsyms);
  if (synthcount > 0)
    {
      asymbol **synth_symbol_table;
      long i;

      make_cleanup (xfree, synthsyms);
      synth_symbol_table = xmalloc (sizeof (asymbol *) * synthcount);
      for (i = 0; i < synthcount; i++)
	synth_symbol_table[i] = synthsyms + i;
      make_cleanup (xfree, synth_symbol_table);
      elf_symtab_read (objfile, ST_SYNTHETIC, synthcount,
		       synth_symbol_table, 1);
    }

  /* Install any minimal symbols that have been collected as the current
     minimal symbols for this objfile.  The debug readers below this point
     should not generate new minimal symbols; if they do it's their
     responsibility to install them.  "mdebug" appears to be the only one
     which will do this.  */

  install_minimal_symbols (objfile);
  do_cleanups (back_to);

  /* Now process debugging information, which is contained in
     special ELF sections.  */

  /* We first have to find them...  */
  bfd_map_over_sections (abfd, elf_locate_sections, (void *) & ei);

  /* ELF debugging information is inserted into the psymtab in the
     order of least informative first - most informative last.  Since
     the psymtab table is searched `most recent insertion first' this
     increases the probability that more detailed debug information
     for a section is found.

     For instance, an object file might contain both .mdebug (XCOFF)
     and .debug_info (DWARF2) sections then .mdebug is inserted first
     (searched last) and DWARF2 is inserted last (searched first).  If
     we don't do this then the XCOFF info is found first - for code in
     an included file XCOFF info is useless.  */

  if (ei.mdebugsect)
    {
      const struct ecoff_debug_swap *swap;

      /* .mdebug section, presumably holding ECOFF debugging
         information.  */
      swap = get_elf_backend_data (abfd)->elf_backend_ecoff_debug_swap;
      if (swap)
	elfmdebug_build_psymtabs (objfile, swap, ei.mdebugsect);
    }
  if (ei.stabsect)
    {
      asection *str_sect;

      /* Stab sections have an associated string table that looks like
         a separate section.  */
      str_sect = bfd_get_section_by_name (abfd, ".stabstr");

      /* FIXME should probably warn about a stab section without a stabstr.  */
      if (str_sect)
	elfstab_build_psymtabs (objfile,
				ei.stabsect,
				str_sect->filepos,
				bfd_section_size (abfd, str_sect));
    }

  if (symtab_create_debug)
    fprintf_unfiltered (gdb_stdlog, "Done reading minimal symbols.\n");

  if (dwarf2_has_info (objfile, NULL))
    {
      /* elf_sym_fns_gdb_index cannot handle simultaneous non-DWARF debug
	 information present in OBJFILE.  If there is such debug info present
	 never use .gdb_index.  */

      if (!objfile_has_partial_symbols (objfile)
	  && dwarf2_initialize_objfile (objfile))
	objfile->sf = &elf_sym_fns_gdb_index;
      else
	{
	  /* It is ok to do this even if the stabs reader made some
	     partial symbols, because OBJF_PSYMTABS_READ has not been
	     set, and so our lazy reader function will still be called
	     when needed.  */
	  objfile->sf = &elf_sym_fns_lazy_psyms;
	}
    }
  /* If the file has its own symbol tables it has no separate debug
     info.  `.dynsym'/`.symtab' go to MSYMBOLS, `.debug_info' goes to
     SYMTABS/PSYMTABS.  `.gnu_debuglink' may no longer be present with
     `.note.gnu.build-id'.

     .gnu_debugdata is !objfile_has_partial_symbols because it contains only
     .symtab, not .debug_* section.  But if we already added .gnu_debugdata as
     an objfile via find_separate_debug_file_in_section there was no separate
     debug info available.  Therefore do not attempt to search for another one,
     objfile->separate_debug_objfile->separate_debug_objfile GDB guarantees to
     be NULL and we would possibly violate it.  */

  else if (!objfile_has_partial_symbols (objfile)
	   && objfile->separate_debug_objfile == NULL
	   && objfile->separate_debug_objfile_backlink == NULL)
    {
      char *debugfile, *build_id_filename;

      debugfile = find_separate_debug_file_by_buildid (objfile,
						       &build_id_filename);

      if (debugfile == NULL)
	debugfile = find_separate_debug_file_by_debuglink (objfile);

      if (debugfile)
	{
	  struct cleanup *cleanup = make_cleanup (xfree, debugfile);
	  bfd *abfd = symfile_bfd_open (debugfile);

	  make_cleanup_bfd_unref (abfd);
	  symbol_file_add_separate (abfd, symfile_flags, objfile);
	  do_cleanups (cleanup);
	}
      /* Check if any separate debug info has been extracted out.  */
      else if (bfd_get_section_by_name (objfile->obfd, ".gnu_debuglink")
	       != NULL)
	debug_print_missing (objfile->name, build_id_filename);

      xfree (build_id_filename);
    }
}

/* Callback to lazily read psymtabs.  */

static void
read_psyms (struct objfile *objfile)
{
  if (dwarf2_has_info (objfile, NULL))
    dwarf2_build_psymtabs (objfile);
}

/* This cleans up the objfile's dbx symfile info, and the chain of
   stab_section_info's, that might be dangling from it.  */

static void
free_elfinfo (void *objp)
{
  struct objfile *objfile = (struct objfile *) objp;
  struct dbx_symfile_info *dbxinfo = DBX_SYMFILE_INFO (objfile);
  struct stab_section_info *ssi, *nssi;

  ssi = dbxinfo->stab_section_info;
  while (ssi)
    {
      nssi = ssi->next;
      xfree (ssi);
      ssi = nssi;
    }

  dbxinfo->stab_section_info = 0;	/* Just say No mo info about this.  */
}


/* Initialize anything that needs initializing when a completely new symbol
   file is specified (not just adding some symbols from another file, e.g. a
   shared library).

   We reinitialize buildsym, since we may be reading stabs from an ELF
   file.  */

static void
elf_new_init (struct objfile *ignore)
{
  stabsread_new_init ();
  buildsym_new_init ();
}

/* Perform any local cleanups required when we are done with a particular
   objfile.  I.E, we are in the process of discarding all symbol information
   for an objfile, freeing up all memory held for it, and unlinking the
   objfile struct from the global list of known objfiles.  */

static void
elf_symfile_finish (struct objfile *objfile)
{
  dwarf2_free_objfile (objfile);
}

/* ELF specific initialization routine for reading symbols.

   It is passed a pointer to a struct sym_fns which contains, among other
   things, the BFD for the file whose symbols are being read, and a slot for
   a pointer to "private data" which we can fill with goodies.

   For now at least, we have nothing in particular to do, so this function is
   just a stub.  */

static void
elf_symfile_init (struct objfile *objfile)
{
  /* ELF objects may be reordered, so set OBJF_REORDERED.  If we
     find this causes a significant slowdown in gdb then we could
     set it in the debug symbol readers only when necessary.  */
  objfile->flags |= OBJF_REORDERED;
}

/* When handling an ELF file that contains Sun STABS debug info,
   some of the debug info is relative to the particular chunk of the
   section that was generated in its individual .o file.  E.g.
   offsets to static variables are relative to the start of the data
   segment *for that module before linking*.  This information is
   painfully squirreled away in the ELF symbol table as local symbols
   with wierd names.  Go get 'em when needed.  */

void
elfstab_offset_sections (struct objfile *objfile, struct partial_symtab *pst)
{
  const char *filename = pst->filename;
  struct dbx_symfile_info *dbx = DBX_SYMFILE_INFO (objfile);
  struct stab_section_info *maybe = dbx->stab_section_info;
  struct stab_section_info *questionable = 0;
  int i;

  /* The ELF symbol info doesn't include path names, so strip the path
     (if any) from the psymtab filename.  */
  filename = lbasename (filename);

  /* FIXME:  This linear search could speed up significantly
     if it was chained in the right order to match how we search it,
     and if we unchained when we found a match.  */
  for (; maybe; maybe = maybe->next)
    {
      if (filename[0] == maybe->filename[0]
	  && filename_cmp (filename, maybe->filename) == 0)
	{
	  /* We found a match.  But there might be several source files
	     (from different directories) with the same name.  */
	  if (0 == maybe->found)
	    break;
	  questionable = maybe;	/* Might use it later.  */
	}
    }

  if (maybe == 0 && questionable != 0)
    {
      complaint (&symfile_complaints,
		 _("elf/stab section information questionable for %s"),
		 filename);
      maybe = questionable;
    }

  if (maybe)
    {
      /* Found it!  Allocate a new psymtab struct, and fill it in.  */
      maybe->found++;
      pst->section_offsets = (struct section_offsets *)
	obstack_alloc (&objfile->objfile_obstack,
		       SIZEOF_N_SECTION_OFFSETS (objfile->num_sections));
      for (i = 0; i < maybe->num_sections; i++)
	(pst->section_offsets)->offsets[i] = maybe->sections[i];
      return;
    }

  /* We were unable to find any offsets for this file.  Complain.  */
  if (dbx->stab_section_info)	/* If there *is* any info, */
    complaint (&symfile_complaints,
	       _("elf/stab section information missing for %s"), filename);
}

/* Implementation of `sym_get_probes', as documented in symfile.h.  */

static VEC (probe_p) *
elf_get_probes (struct objfile *objfile)
{
  VEC (probe_p) *probes_per_objfile;

  /* Have we parsed this objfile's probes already?  */
  probes_per_objfile = objfile_data (objfile, probe_key);

  if (!probes_per_objfile)
    {
      int ix;
      const struct probe_ops *probe_ops;

      /* Here we try to gather information about all types of probes from the
	 objfile.  */
      for (ix = 0; VEC_iterate (probe_ops_cp, all_probe_ops, ix, probe_ops);
	   ix++)
	probe_ops->get_probes (&probes_per_objfile, objfile);

      if (probes_per_objfile == NULL)
	{
	  VEC_reserve (probe_p, probes_per_objfile, 1);
	  gdb_assert (probes_per_objfile != NULL);
	}

      set_objfile_data (objfile, probe_key, probes_per_objfile);
    }

  return probes_per_objfile;
}

/* Implementation of `sym_get_probe_argument_count', as documented in
   symfile.h.  */

static unsigned
elf_get_probe_argument_count (struct probe *probe)
{
  return probe->pops->get_probe_argument_count (probe);
}

/* Implementation of `sym_evaluate_probe_argument', as documented in
   symfile.h.  */

static struct value *
elf_evaluate_probe_argument (struct probe *probe, unsigned n)
{
  return probe->pops->evaluate_probe_argument (probe, n);
}

/* Implementation of `sym_compile_to_ax', as documented in symfile.h.  */

static void
elf_compile_to_ax (struct probe *probe,
		   struct agent_expr *expr,
		   struct axs_value *value,
		   unsigned n)
{
  probe->pops->compile_to_ax (probe, expr, value, n);
}

/* Implementation of `sym_relocate_probe', as documented in symfile.h.  */

static void
elf_symfile_relocate_probe (struct objfile *objfile,
			    struct section_offsets *new_offsets,
			    struct section_offsets *delta)
{
  int ix;
  VEC (probe_p) *probes = objfile_data (objfile, probe_key);
  struct probe *probe;

  for (ix = 0; VEC_iterate (probe_p, probes, ix, probe); ix++)
    probe->pops->relocate (probe, ANOFFSET (delta, SECT_OFF_TEXT (objfile)));
}

/* Helper function used to free the space allocated for storing SystemTap
   probe information.  */

static void
probe_key_free (struct objfile *objfile, void *d)
{
  int ix;
  VEC (probe_p) *probes = d;
  struct probe *probe;

  for (ix = 0; VEC_iterate (probe_p, probes, ix, probe); ix++)
    probe->pops->destroy (probe);

  VEC_free (probe_p, probes);
}



/* Implementation `sym_probe_fns', as documented in symfile.h.  */

static const struct sym_probe_fns elf_probe_fns =
{
  elf_get_probes,		/* sym_get_probes */
  elf_get_probe_argument_count,	/* sym_get_probe_argument_count */
  elf_evaluate_probe_argument,	/* sym_evaluate_probe_argument */
  elf_compile_to_ax,		/* sym_compile_to_ax */
  elf_symfile_relocate_probe,	/* sym_relocate_probe */
};

/* Register that we are able to handle ELF object file formats.  */

static const struct sym_fns elf_sym_fns =
{
  bfd_target_elf_flavour,
  elf_new_init,			/* init anything gbl to entire symtab */
  elf_symfile_init,		/* read initial info, setup for sym_read() */
  elf_symfile_read,		/* read a symbol file into symtab */
  NULL,				/* sym_read_psymbols */
  elf_symfile_finish,		/* finished with file, cleanup */
  default_symfile_offsets,	/* Translate ext. to int. relocation */
  elf_symfile_segments,		/* Get segment information from a file.  */
  NULL,
  default_symfile_relocate,	/* Relocate a debug section.  */
  &elf_probe_fns,		/* sym_probe_fns */
  &psym_functions
};

/* The same as elf_sym_fns, but not registered and lazily reads
   psymbols.  */

static const struct sym_fns elf_sym_fns_lazy_psyms =
{
  bfd_target_elf_flavour,
  elf_new_init,			/* init anything gbl to entire symtab */
  elf_symfile_init,		/* read initial info, setup for sym_read() */
  elf_symfile_read,		/* read a symbol file into symtab */
  read_psyms,			/* sym_read_psymbols */
  elf_symfile_finish,		/* finished with file, cleanup */
  default_symfile_offsets,	/* Translate ext. to int. relocation */
  elf_symfile_segments,		/* Get segment information from a file.  */
  NULL,
  default_symfile_relocate,	/* Relocate a debug section.  */
  &elf_probe_fns,		/* sym_probe_fns */
  &psym_functions
};

/* The same as elf_sym_fns, but not registered and uses the
   DWARF-specific GNU index rather than psymtab.  */
static const struct sym_fns elf_sym_fns_gdb_index =
{
  bfd_target_elf_flavour,
  elf_new_init,			/* init anything gbl to entire symab */
  elf_symfile_init,		/* read initial info, setup for sym_red() */
  elf_symfile_read,		/* read a symbol file into symtab */
  NULL,				/* sym_read_psymbols */
  elf_symfile_finish,		/* finished with file, cleanup */
  default_symfile_offsets,	/* Translate ext. to int. relocatin */
  elf_symfile_segments,		/* Get segment information from a file.  */
  NULL,
  default_symfile_relocate,	/* Relocate a debug section.  */
  &elf_probe_fns,		/* sym_probe_fns */
  &dwarf2_gdb_index_functions
};

/* STT_GNU_IFUNC resolver vector to be installed to gnu_ifunc_fns_p.  */

static const struct gnu_ifunc_fns elf_gnu_ifunc_fns =
{
  elf_gnu_ifunc_resolve_addr,
  elf_gnu_ifunc_resolve_name,
  elf_gnu_ifunc_resolver_stop,
  elf_gnu_ifunc_resolver_return_stop
};

void
_initialize_elfread (void)
{
  probe_key = register_objfile_data_with_cleanup (NULL, probe_key_free);
  add_symtab_fns (&elf_sym_fns);

  elf_objfile_gnu_ifunc_cache_data = register_objfile_data ();
  gnu_ifunc_fns_p = &elf_gnu_ifunc_fns;

  add_setshow_zinteger_cmd ("build-id-verbose", no_class, &build_id_verbose,
			    _("\
Set debugging level of the build-id locator."), _("\
Show debugging level of the build-id locator."), _("\
Level 1 (default) enables printing the missing debug filenames,\n\
level 2 also prints the parsing of binaries to find the identificators."),
			    NULL,
			    show_build_id_verbose,
			    &setlist, &showlist);

  observer_attach_executable_changed (debug_print_executable_changed);
}
