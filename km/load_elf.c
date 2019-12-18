/*
 * Copyright © 2018-2019 Kontain Inc. All rights reserved.
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
#include <sys/stat.h>

#include "km.h"
#include "km_gdb.h"
#include "km_mem.h"

km_payload_t km_guest;
km_payload_t km_dynlinker;
char* km_dynlinker_file = "/opt/kontain/lib64/libc.so.km";

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
static void load_extent(int fd, const GElf_Phdr* phdr, km_gva_t base)
{
   km_gva_t top = phdr->p_paddr + phdr->p_memsz + base;
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
   km_kma_t addr = km_gva_to_kma_nocheck(phdr->p_paddr) + base;
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

static inline int km_find_hook_symbols(Elf* e, km_gva_t adjust)
{
   int all_found = 0;
   for (Elf_Scn* scn = NULL; (scn = elf_nextscn(e, scn)) != NULL;) {
      GElf_Shdr shdr;

      gelf_getshdr(scn, &shdr);
      if (shdr.sh_type == SHT_SYMTAB) {
         Elf_Data* data = elf_getdata(scn, NULL);

         for (int i = 0; i < shdr.sh_size / shdr.sh_entsize; i++) {
            GElf_Sym sym;

            gelf_getsym(data, i, &sym);
            if (sym.st_info == ELF64_ST_INFO(STB_GLOBAL, STT_FUNC) &&
                strcmp(elf_strptr(e, shdr.sh_link, sym.st_name), KM_INT_HNDL_SYM_NAME) == 0) {
               km_guest.km_handlers = sym.st_value + adjust;
            } else if (sym.st_info == ELF64_ST_INFO(STB_GLOBAL, STT_FUNC) &&
                       strcmp(elf_strptr(e, shdr.sh_link, sym.st_name), KM_SIG_RTRN_SYM_NAME) == 0) {
               km_guest.km_sigreturn = sym.st_value + adjust;
            } else if (sym.st_info == ELF64_ST_INFO(STB_GLOBAL, STT_FUNC) &&
                       strcmp(elf_strptr(e, shdr.sh_link, sym.st_name), KM_CLONE_CHILD_SYM_NAME) == 0) {
               km_guest.km_clone_child = sym.st_value + adjust;
            }
            if (km_guest.km_handlers != 0 && km_guest.km_sigreturn != 0 && km_guest.km_clone_child) {
               all_found = 1;
               break;
            }
         }
         break;
      }
   }
   return all_found;
}

static Elf* km_open_elf_file(km_payload_t* payload, int* fd)
{
   Elf* e;
   if ((*fd = open(payload->km_filename, O_RDONLY, 0)) < 0) {
      warn("open %s failed", payload->km_filename);
      return NULL;
   }
   if ((e = elf_begin(*fd, ELF_C_READ, NULL)) == NULL) {
      warnx("elf_begin() failed: %s", elf_errmsg(-1));
      return NULL;
   }
   if (elf_kind(e) != ELF_K_ELF) {
      warnx("%s is not an ELF object", payload->km_filename);
      return NULL;
   }
   GElf_Ehdr* ehdr = &payload->km_ehdr;
   if (gelf_getehdr(e, ehdr) == NULL) {
      errx(2, "gelf_getehdr %s", elf_errmsg(-1));
   }
   if ((payload->km_phdr = malloc(sizeof(Elf64_Phdr) * ehdr->e_phnum)) == NULL) {
      err(2, "no memory for elf program headers");
   }

   payload->km_min_vaddr = -1U;
   for (int i = 0; i < ehdr->e_phnum; i++) {
      GElf_Phdr* phdr = &payload->km_phdr[i];

      if (gelf_getphdr(e, i, phdr) == NULL) {
         errx(2, "gelf_getphrd %i, %s", i, elf_errmsg(-1));
      }
      if (phdr->p_type == PT_LOAD && phdr->p_vaddr < payload->km_min_vaddr) {
         payload->km_min_vaddr = phdr->p_vaddr;
      }
      if (phdr->p_type == PT_INTERP) {
         payload->km_interp_vaddr = phdr->p_vaddr;
         payload->km_interp_len = phdr->p_filesz;
      }
   }
   return e;
}
/*
 * load the dynamic linker. Currently only support MUSL dynlink built with KM hypercalls.
 */
static void load_dynlink(km_gva_t interp_vaddr, uint64_t interp_len, km_gva_t interp_adjust)
{
   // Make sure interpreter string contains KM dynlink marker.
   char* interp_kma = km_gva_to_kma(interp_vaddr + interp_adjust);
   if (interp_kma == NULL || km_gva_to_kma(interp_vaddr + interp_adjust + interp_len - 1) == NULL) {
      errx(2,
           "%s: PT_INTERP vaddr map error: vaddr=0x%lx len=0x%lx adjust=0x%lx",
           __FUNCTION__,
           interp_vaddr,
           interp_len,
           interp_adjust);
   }
   if (strncmp(interp_kma, KM_DYNLINKER_STR, interp_len) != 0) {
      errx(2, "PT_INTERP does not contain km marker. expect:'%s' got:'%s'", KM_DYNLINKER_STR, interp_kma);
   }

   struct stat st;
   if (stat(km_dynlinker_file, &st) != 0) {
      err(2, "KM dynamic linker %s", km_dynlinker_file);
   }
   km_dynlinker.km_filename = km_dynlinker_file;

   Elf* e;
   int fd;
   GElf_Ehdr* ehdr = &km_dynlinker.km_ehdr;
   if ((e = km_open_elf_file(&km_dynlinker, &fd)) == NULL) {
      errx(2, "%s km_open_elf failed: %s", __FUNCTION__, km_dynlinker.km_filename);
   }

   km_gva_t base = km_mem_brk(0);
   if (base != roundup(base, KM_PAGE_SIZE)) {
      // What about wasted bytes between base and roundup(base)?
      base = km_mem_brk(roundup(base, KM_PAGE_SIZE));
   }
   km_gva_t adjust = km_dynlinker.km_load_adjust = base - km_dynlinker.km_min_vaddr;

   km_find_hook_symbols(e, adjust);

   for (int i = 0; i < ehdr->e_phnum; i++) {
      GElf_Phdr* phdr = &km_dynlinker.km_phdr[i];
      if (phdr->p_type == PT_LOAD) {
         load_extent(fd, phdr, base);
      }
   }

   (void)elf_end(e);
   (void)close(fd);
}

/*
 * Read elf executable file and initialize mem with the content of the program
 * segments. Set entry point.
 * All errors are fatal.
 */
uint64_t km_load_elf(const char* file)
{
   char fn[strlen(file + 3)];
   int fd;
   Elf* e;
   GElf_Ehdr* ehdr = &km_guest.km_ehdr;

   if (elf_version(EV_CURRENT) == EV_NONE) {
      errx(2, "ELF library initialization failed: %s", elf_errmsg(-1));
   }
   if (strcmp(".km", file + strlen(file) - 3) != 0 && strcmp(".kmd", file + strlen(file) - 4) != 0) {
      sprintf(fn, "%s.km", file);
      file = fn;
   }
   if ((km_guest.km_filename = realpath(file, NULL)) == NULL) {
      err(2, "%s realpath failed: %s", __FUNCTION__, file);
   }

   e = km_open_elf_file(&km_guest, &fd);

   km_gva_t adjust = GUEST_MEM_START_VA - km_guest.km_min_vaddr;
   /*
    * Read symbol table and look for symbols of interest to KM
    */
   km_find_hook_symbols(e, adjust);
   if (km_guest.km_interp_vaddr == 0 && (km_guest.km_handlers == 0 || km_guest.km_sigreturn == 0)) {
      errx(1,
           "Non-KM binary: cannot find interrupt handler%s or sigreturn%s. Trying to "
           "run regular Linux executable in KM?",
           km_guest.km_handlers == 0 ? "(*)" : "",
           km_guest.km_sigreturn == 0 ? "(*)" : "");
   }
   /*
    * Read program headers, store them in km_guest for future use, and process PT_LOAD ones
    */
   for (int i = 0; i < ehdr->e_phnum; i++) {
      GElf_Phdr* phdr = &km_guest.km_phdr[i];
      if (phdr->p_type == PT_LOAD) {
         load_extent(fd, phdr, adjust);
      }
   }
   (void)elf_end(e);
   (void)close(fd);
   if (km_guest.km_interp_vaddr != 0) {
      load_dynlink(km_guest.km_interp_vaddr, km_guest.km_interp_len, adjust);
   }
   km_guest.km_load_adjust = adjust;
   return adjust;
}
