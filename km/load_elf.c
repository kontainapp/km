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

static void my_mmap(int fd, void* buf, size_t count, off_t offset)
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
static void load_extent(int fd, const GElf_Phdr* phdr)
{
   km_gva_t top = phdr->p_paddr + phdr->p_memsz;
   /*
    * There is no memory in the first 2MB of virtual address space. We use gcc-km.spec file to drive
    * memory allocation during linking, based on default linking script. The relevant line is:
    * ``... -Ttext-segment=0x1FF000 ... -z norelro %(old_link) --build-id=none''
    * In combination with the default linking script this lands the beginning of text segment at
    * 2MB. Obviously 0x1FF000 is one page before that, but the linking script does
    * ``. = SEGMENT_START("text-segment", 0x400000) + SIZEOF_HEADERS;'' and later
    * ``. = ALIGN(CONSTANT (MAXPAGESIZE));''
    * which adds that page. Normally that page contains program headers and build-if. The former
    * doesn't apply to us as we are statically linked, so the space is just wasted. --build-id=none
    * removes the latter, so the page simply stay empty, and first loadable segment starts at 2MB as
    * we want.
    *
    * We also have a test in the test suit to check for 2MB start.
    */
   assert(top >= GUEST_MEM_START_VA);

   /* Extent memory if necessary */
   if (top >= km_mem_brk(0)) {
      if (km_mem_brk(top) != top) {
         err(2, "No memory to load elf");
      }
   }

   Elf64_Xword p_memsz = phdr->p_memsz;
   Elf64_Xword p_filesz = phdr->p_filesz;
   km_kma_t addr = km_gva_to_kma(phdr->p_paddr);
   uint64_t extra = addr - (km_kma_t)rounddown((uint64_t)addr, KM_PAGE_SIZE);
   my_mmap(fd, addr - extra, p_filesz + extra, phdr->p_offset - extra);
   memset(addr + p_filesz, 0, p_memsz - p_filesz);
   int pr = 0;
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
   if (mprotect(addr - extra, p_memsz + extra, pr) < 0) {
      err(2, "failed to set guest memory protection");
   }
}

static void my_pread(int fd, void* buf, size_t count, off_t offset)
{
   int i, rc;

   for (i = 0, rc = 0; i < count; i += rc) {
      // rc == 0 means EOF which to us means error in the format
      if ((rc = pread(fd, buf + i, count - i, offset + i)) <= 0 && rc != EINTR) {
         err(2, "error reading elf");
      }
   }
}

static void tls_extent(int fd, GElf_Phdr* phdr)
{
   km_main_tls.align = phdr->p_align;
   km_main_tls.len = phdr->p_filesz;
   km_main_tls.size = phdr->p_memsz;
   if ((km_main_tls.image = malloc(phdr->p_filesz)) == NULL) {
      errx(2, "no memory to store TLS initialized data");
   }
   my_pread(fd, km_main_tls.image, phdr->p_filesz, phdr->p_offset);
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
      if (phdr->p_type == PT_LOAD) {
         load_extent(fd, phdr);
      }
      if (phdr->p_type == PT_TLS) {
         tls_extent(fd, phdr);
      }
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
                strcmp(elf_strptr(e, shdr.sh_link, sym.st_name), KM_SIG_RTRN_SYM_NAME) == 0) {
               km_guest.km_sigreturn = sym.st_value;
            }
            if (km_guest.km_libc != 0 && km_guest.km_handlers != 0 && km_guest.km_tsd_size != 0 &&
                km_guest.km_sigreturn != 0) {
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
