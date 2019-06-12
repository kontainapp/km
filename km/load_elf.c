/*
 * Copyright © 2018 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication of
 * such source code. Disclosure of this source code or any related proprietary
 * information is strictly prohibited without the express written permission of
 * Kontain Inc.
 */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/param.h>

#include "km.h"
#include "km_gdb.h"
#include "km_mem.h"

km_payload_t km_guest;

static void my_pread(int fd, void* buf, size_t count, off_t offset)
{
   if (count > 0) {
      if (mmap(buf, roundup(count, KM_PAGE_SIZE), PROT_WRITE, MAP_PRIVATE | MAP_FIXED, fd, offset) ==
          MAP_FAILED) {
         err(2, "error mmap elf");
      }
   }
}

/*
 * Process single program extent:
 * - adjust break so there is enough memory
 * - loop over memory regions this extent covering, read/memset the content and set mprotect.
 */
static void load_extent(int fd, GElf_Phdr* phdr)
{
   km_kma_t addr;
   uint64_t size, filesize;
   km_gva_t top;
   int idx, pr;

   /* Extent memory if neccesary */
   top = phdr->p_paddr + phdr->p_memsz;
   if (top >= km_mem_brk(0)) {
      if (km_mem_brk(top) != top) {
         err(2, "No memory to load elf");
      }
   }

   /*
    * As memory is structured in regions, loop over the covered regions to make
    * sure not to cross the boundary.
    *
    * The virtual address space is contiguous, there are no holes or anything
    * observable on the boundary. But the monitor addresses are most likely not
    * contiguos at the boundary. Hence we have to split the extent into two or
    * more parts, splitting it on regions boundary so pread() and friends could
    * work as expected. In the guest since virtual memory machinery works
    * transparently, there are no issues.
    *
    * Preserve the values in phdr. They will be needed to drop a core.
    */
   Elf64_Addr p_paddr = phdr->p_paddr;
   Elf64_Xword p_memsz = phdr->p_memsz;
   Elf64_Off p_offset = phdr->p_offset;
   Elf64_Xword p_filesz = phdr->p_filesz;
   do {
      idx = gva_to_memreg_idx(p_paddr);
      addr = km_gva_to_kma(p_paddr);
      size = MIN(p_memsz, memreg_top(idx) - p_paddr);
      filesize = MIN(p_filesz, size);
      my_pread(fd, addr, filesize, p_offset);
      memset(addr + filesize, 0, size - filesize);
      pr = 0;
      if (phdr->p_flags & PF_R) {
         pr |= PROT_READ;
      }
      if (phdr->p_flags & PF_W) {
         pr |= PROT_WRITE;
      }
      if (phdr->p_flags & PF_X) {
         pr |= PROT_EXEC;
         {
            // When debugging, make sure all EXEC sections are
            // writable so sw breakpoints can be inserted.
            // TODO - manage protection dynamically on set/clear breakpoints
            if (km_gdb_is_enabled() == 1) {
               pr |= PROT_WRITE;
            }
         }
      }
      if (mprotect(addr, size, pr) < 0) {
         err(2, "failed to set guest memory protection");
      }
      p_paddr += size;
      p_memsz -= size;
      p_offset += filesize;
      p_filesz -= filesize;
   } while (p_memsz > 0);
}

/*
 * Read elf executable file and initialize mem with the content of the program
 * segments. Set entry point.
 * All errors are fatal.
 */
int km_load_elf(const char* file)
{
   int fd;
   Elf* e;
   GElf_Ehdr* ehdr = &km_guest.km_ehdr;

   if (elf_version(EV_CURRENT) == EV_NONE) {
      errx(2, "ELF library initialization failed: %s", elf_errmsg(-1));
   }
   if ((fd = open(file, O_RDONLY, 0)) < 0) {
      err(2, "open %s failed", file);
   }
   if ((e = elf_begin(fd, ELF_C_READ, NULL)) == NULL) {
      errx(2, "elf_begin() failed: %s", elf_errmsg(-1));
   }
   if (elf_kind(e) != ELF_K_ELF) {
      errx(2, "%s is not an ELF object", file);
   }
   if (gelf_getehdr(e, ehdr) == NULL) {
      errx(2, "gelf_getehdr %s", elf_errmsg(-1));
   }
   if ((km_guest.km_phdr = malloc(sizeof(Elf64_Phdr) * ehdr->e_phnum)) == NULL) {
      err(2, "no memory for elf program headers");
   }
   /*
    * Read program headers, store them in km_guest for future use, and process PT_LOAD ones
    */
   for (int i = 0; i < ehdr->e_phnum; i++) {
      GElf_Phdr* phdr = &km_guest.km_phdr[i];

      if (gelf_getphdr(e, i, phdr) == NULL) {
         errx(2, "gelf_getphrd %i, %s", i, elf_errmsg(-1));
      }
      if (phdr->p_type != PT_LOAD) {
         continue;
      }
      load_extent(fd, phdr);
   }
   /*
    * Read symbol table and look for symbols of interest to KM
    */
   for (Elf_Scn* scn = NULL; (scn = elf_nextscn(e, scn)) != NULL;) {
      GElf_Shdr shdr;

      gelf_getshdr(scn, &shdr);
      if (shdr.sh_type == SHT_SYMTAB) {
         Elf_Data* data = elf_getdata(scn, NULL);

         for (int i = 0; i < shdr.sh_size / shdr.sh_entsize; i++) {
            GElf_Sym sym;

            gelf_getsym(data, i, &sym);
            if (sym.st_info == ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT) &&
                strcmp(elf_strptr(e, shdr.sh_link, sym.st_name), KM_LIBC_SYM_NAME) == 0) {
               km_guest.km_libc = sym.st_value;
            }
            if (sym.st_info == ELF64_ST_INFO(STB_GLOBAL, STT_FUNC) &&
                strcmp(elf_strptr(e, shdr.sh_link, sym.st_name), KM_INT_HNDL_SYM_NAME) == 0) {
               km_guest.km_handlers = sym.st_value;
            }
            if ((sym.st_info == ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT) ||
                 sym.st_info == ELF64_ST_INFO(STB_WEAK, STT_OBJECT)) &&
                strcmp(elf_strptr(e, shdr.sh_link, sym.st_name), KM_TSD_SIZE_SYM_NAME) == 0) {
               km_guest.km_tsd_size = sym.st_value;
            }
            if (sym.st_info == ELF64_ST_INFO(STB_GLOBAL, STT_FUNC) &&
                strcmp(elf_strptr(e, shdr.sh_link, sym.st_name), KM_SIG_HNDL_SYM_NAME) == 0) {
               km_guest.km_sighandle = sym.st_value;
            }
            if (km_guest.km_libc != 0 && km_guest.km_handlers != 0 && km_guest.km_tsd_size != 0 && km_guest.km_sighandle != 0) {
               break;
            }
         }
         break;
      }
   }
   (void)elf_end(e);
   (void)close(fd);
   return 0;
}
