/*
 * Copyright © 2018-2020 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication of
 * such source code. Disclosure of this source code or any related proprietary
 * information is strictly prohibited without the express written permission of
 * Kontain Inc.
 */

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
#include "km_filesys.h"
#include "km_gdb.h"
#include "km_mem.h"

km_payload_t km_guest;
km_payload_t km_dynlinker;
char* km_dynlinker_file = "/opt/kontain/lib64/libc.so";

static void my_mmap(int fd, void* buf, size_t count, off_t offset)
{
   if (count > 0) {
      if (mmap(buf, roundup(count, KM_PAGE_SIZE), PROT_WRITE, MAP_PRIVATE | MAP_FIXED, fd, offset) ==
          MAP_FAILED) {
         km_err(2, "error mmap elf");
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
    * There is no memory in the first 2MB of virtual address space. We use flags in kontain-gcc
    * script to drive memory allocation during linking. The relevant line is:
    * ``... -Ttext-segment=0x1FF000 ... ''. This lands the beginning of text segment at
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
         km_err(2, "No memory to load elf");
      }
   }

   Elf64_Xword p_memsz = phdr->p_memsz;
   Elf64_Xword p_filesz = phdr->p_filesz;
   km_kma_t addr = km_gva_to_kma_nocheck(phdr->p_paddr) + base;
   uint64_t extra = addr - (km_kma_t)rounddown((uint64_t)addr, KM_PAGE_SIZE);
   my_mmap(fd, addr - extra, p_filesz + extra, phdr->p_offset - extra);
   memset(addr + p_filesz, 0, p_memsz - p_filesz);
   int pr = prot_elf_to_mmap(phdr->p_flags);
   if (mprotect(addr - extra, p_memsz + extra, protection_adjust(pr)) < 0) {
      km_err(2, "failed to set guest memory protection");
   }
}

static void km_find_hook_symbols(Elf* e, km_gva_t adjust)
{
   for (Elf_Scn* scn = NULL; (scn = elf_nextscn(e, scn)) != NULL;) {
      GElf_Shdr shdr;

      gelf_getshdr(scn, &shdr);
      if (shdr.sh_type == SHT_SYMTAB) {   // assume there is only one symtab, break after processing
         Elf_Data* data = elf_getdata(scn, NULL);

         for (int i = 0; i < shdr.sh_size / shdr.sh_entsize; i++) {
            GElf_Sym sym;

            gelf_getsym(data, i, &sym);
            if (sym.st_info == ELF64_ST_INFO(STB_GLOBAL, STT_FUNC) &&
                strcmp(elf_strptr(e, shdr.sh_link, sym.st_name), KM_DLOPEN_SYM_NAME) == 0) {
               km_guest.km_dlopen = sym.st_value + adjust;
            }
            if (km_guest.km_dlopen != 0) {
               break;
            }
         }
         break;
      }
   }
   return;
}

Elf* km_open_elf_file(char* filename, km_payload_t* payload, int* fd)
{
   Elf* e;
   if ((*fd = km_internal_open(filename, O_RDONLY)) < 0) {
      km_warn("open %s failed", filename);
      return NULL;
   }
   payload->km_filename = filename;
   if ((e = elf_begin(*fd, ELF_C_READ, NULL)) == NULL) {
      km_warnx("elf_begin() failed: %s", elf_errmsg(-1));
      return NULL;
   }
   if (elf_kind(e) != ELF_K_ELF) {
      km_warnx("%s is not an ELF object", payload->km_filename);
      return NULL;
   }
   GElf_Ehdr* ehdr = &payload->km_ehdr;
   if (gelf_getehdr(e, ehdr) == NULL) {
      km_errx(2, "gelf_getehdr %s", elf_errmsg(-1));
   }
   if ((payload->km_phdr = malloc(sizeof(Elf64_Phdr) * ehdr->e_phnum)) == NULL) {
      km_err(2, "no memory for elf program headers");
   }

   payload->km_min_vaddr = -1U;
   for (int i = 0; i < ehdr->e_phnum; i++) {
      GElf_Phdr* phdr = &payload->km_phdr[i];

      if (gelf_getphdr(e, i, phdr) == NULL) {
         km_errx(2, "gelf_getphrd %i, %s", i, elf_errmsg(-1));
      }
      // TODO: This doesn't apply to snapshots. SHould move to load_elf().
      if (phdr->p_type == PT_LOAD && phdr->p_vaddr < payload->km_min_vaddr) {
         payload->km_min_vaddr = phdr->p_vaddr;
      }
      if (phdr->p_type == PT_INTERP) {
         payload->km_interp_vaddr = phdr->p_vaddr;
         payload->km_interp_len = phdr->p_filesz;
      }
      if (phdr->p_type == PT_DYNAMIC) {
         payload->km_dynamic_vaddr = phdr->p_vaddr;
         payload->km_dynamic_len = phdr->p_filesz;
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
   char* filename = NULL;
   if (interp_kma == NULL || km_gva_to_kma(interp_vaddr + interp_adjust + interp_len - 1) == NULL) {
      km_errx(2,
              "PT_INTERP vaddr map error: vaddr=0x%lx len=0x%lx adjust=0x%lx",
              interp_vaddr,
              interp_len,
              interp_adjust);
   }
   if (strncmp(interp_kma, KM_DYNLINKER_STR, interp_len) != 0) {
      // Use the dynamic linker in the .interp section
      filename = interp_kma;
   } else {
      // Use the dynamic linker mentioned on  the command line
      filename = km_dynlinker_file;
   }

   struct stat st;
   if (stat(filename, &st) != 0) {
      km_err(2, "KM dynamic linker %s", filename);
   }

   Elf* e;
   int fd;
   if ((e = km_open_elf_file(filename, &km_dynlinker, &fd)) == NULL) {
      km_errx(2, "km_open_elf failed: %s", filename);
   }

   km_gva_t base = km_mem_brk(0);
   if (base != roundup(base, KM_PAGE_SIZE)) {
      // What about wasted bytes between base and roundup(base)?
      base = km_mem_brk(roundup(base, KM_PAGE_SIZE));
   }
   km_gva_t adjust = km_dynlinker.km_load_adjust = base - km_dynlinker.km_min_vaddr;

   km_find_hook_symbols(e, adjust);

   GElf_Ehdr* ehdr = &km_dynlinker.km_ehdr;
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
 * Read elf executable file and initialize mem with the content of the program segments. Set entry
 * point. All errors are fatal.
 */
uint64_t km_load_elf(char* file)
{
   int fd;
   Elf* e;
   GElf_Ehdr* ehdr = &km_guest.km_ehdr;

   if (elf_version(EV_CURRENT) == EV_NONE) {
      km_errx(2, "ELF library initialization failed: %s", elf_errmsg(-1));
   }
   if ((e = km_open_elf_file(file, &km_guest, &fd)) == NULL) {
      km_errx(2, "km_open_elf failed: %s", file);
   }

   km_gva_t adjust = 0;
   /*
    * Tell static vs dynamic executable.
    *
    * DYNAMIC section indicates dynamically linked executable or static PIE. The later also has
    * PT_INTERPRETER.
    *
    * We load pure static (no DYNAMIC) at the addresses in ELF file, i.e. adjust == 0. For our own
    * .km files they will load starting at GUEST_MEM_START_VA because its the way we build them.
    * Others will go where they request, typical Linux exec would start at 4MB.
    *
    * We load DYNAMIC starting at GUEST_MEM_START_VA, adjust will tell how much shift there was. For
    * loading PIE that doesn't matter, but we need to keep it for coredump and such. For dynamic
    * linking adjust is passed to the dynamic linker so it know how to do relocations.
    */
   if (km_guest.km_dynamic_vaddr != 0 && km_guest.km_dynamic_len != 0) {
      adjust = GUEST_MEM_START_VA - km_guest.km_min_vaddr;
   }
   /*
    * Read symbol table and look for symbols of interest to KM
    */
   km_find_hook_symbols(e, adjust);

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

   // TODO: need to verify the payload is a km executable, issue #512.

   return adjust;
}
