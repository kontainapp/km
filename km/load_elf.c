/*
 * Copyright 2021 Kontain Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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

static void km_find_dlopen(km_elf_t* e, km_gva_t adjust)
{
   if (e->symidx == -1 || e->stridx == -1) {
      return;
   }

   int nsym = e->shdr[e->symidx].sh_size / e->shdr[e->symidx].sh_entsize;
   Elf64_Sym* symtab = (Elf64_Sym*) malloc(e->shdr[e->symidx].sh_size);
   if (fseek(e->file, e->shdr[e->symidx].sh_offset, SEEK_SET) != 0) {
      km_err(2, "fseek failure");
   }
   int nread = fread(symtab, 1, e->shdr[e->symidx].sh_size, e->file);
   if (nread != e->shdr[e->symidx].sh_size) {
      km_err(2, "symbol table read failure");
   } 

   char *strings = malloc(e->shdr[e->stridx].sh_size);
   if (fseek(e->file, e->shdr[e->stridx].sh_offset, SEEK_SET) != 0) {
      km_err(2, "fseek failure");
   }
   nread = fread(strings, 1, e->shdr[e->stridx].sh_size, e->file);
   if (nread != e->shdr[e->stridx].sh_size) {
      km_err(2, "strings table read failure");
   } 

   for (int i = 0; i < nsym; i++) {
      Elf64_Sym sym = symtab[i];

      if (sym.st_info == ELF64_ST_INFO(STB_GLOBAL, STT_FUNC) &&
          strcmp(&strings[sym.st_name], KM_DLOPEN_SYM_NAME) == 0) {
         km_guest.km_dlopen = sym.st_value + adjust;
         free(strings);
         free(symtab);
         return;
      }
   }
   free(strings);
   free(symtab);
   return;
}


/*
 * load the dynamic linker. Currently only support MUSL dynlink built with KM hypercalls.
 */
static void load_dynlink(km_gva_t interp_vaddr, uint64_t interp_len, km_gva_t interp_adjust)
{
   // Make sure interpreter string contains KM dynlink marker.
   char* interp_kma = km_gva_to_kma(interp_vaddr + interp_adjust);
   if (interp_kma == NULL || km_gva_to_kma(interp_vaddr + interp_adjust + interp_len - 1) == NULL) {
      km_errx(2,
              "PT_INTERP vaddr map error: vaddr=0x%lx len=0x%lx adjust=0x%lx",
              interp_vaddr,
              interp_len,
              interp_adjust);
   }
   km_dynlinker.km_filename = interp_kma;

   km_elf_t* e = km_open_elf_file(km_dynlinker.km_filename);
   km_gva_t base = km_mem_brk(0);
   if (base != roundup(base, KM_PAGE_SIZE)) {
      // What about wasted bytes between base and roundup(base)?
      base = km_mem_brk(roundup(base, KM_PAGE_SIZE));
   }
   km_dynlinker.km_ehdr = *e->ehdr;
   if ((km_dynlinker.km_phdr = malloc(sizeof(Elf64_Phdr) * km_dynlinker.km_ehdr.e_phnum)) == NULL) {
      km_err(2, "no memory for elf program headers");
   }

   km_dynlinker.km_min_vaddr = -1U;
   for (int i = 0; i < km_dynlinker.km_ehdr.e_phnum; i++) {
      Elf64_Phdr* phdr = &km_dynlinker.km_phdr[i];

      *phdr = e->phdr[i];
      if (phdr->p_type == PT_LOAD) {
         load_extent(fileno(e->file), phdr, base);
         if (phdr->p_vaddr < km_dynlinker.km_min_vaddr) {
            km_dynlinker.km_min_vaddr = phdr->p_vaddr;
         }
      }
   }

   km_dynlinker.km_load_adjust = base - km_dynlinker.km_min_vaddr;
   km_find_dlopen(e, km_dynlinker.km_load_adjust);
   km_close_elf_file(e);
}

/*
 * Read elf executable file and initialize mem with the content of the program segments. Set entry
 * point. All errors are fatal.
 */
uint64_t km_load_elf(km_elf_t* e)
{
   km_guest.km_ehdr = *e->ehdr;
   if ((km_guest.km_phdr = malloc(sizeof(Elf64_Phdr) * km_guest.km_ehdr.e_phnum)) == NULL) {
      km_err(2, "no memory for elf program headers");
   }
   km_guest.km_filename = e->path;

   /*
    * Read program headers, store them in km_guest for future use
    */
   km_guest.km_min_vaddr = -1U;
   for (int i = 0; i < km_guest.km_ehdr.e_phnum; i++) {
      GElf_Phdr* phdr = &km_guest.km_phdr[i];
      *phdr = e->phdr[i];

      if (phdr->p_type == PT_LOAD && phdr->p_vaddr < km_guest.km_min_vaddr) {
         km_guest.km_min_vaddr = phdr->p_vaddr;
      }
      if (phdr->p_type == PT_INTERP) {
         km_guest.km_interp_vaddr = phdr->p_vaddr;
         km_guest.km_interp_len = phdr->p_filesz;
      }
      if (phdr->p_type == PT_DYNAMIC) {
         km_guest.km_dynamic_vaddr = phdr->p_vaddr;
         km_guest.km_dynamic_len = phdr->p_filesz;
      }
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
    * process PT_LOAD program headers
    */
   for (int i = 0; i < km_guest.km_ehdr.e_phnum; i++) {
      GElf_Phdr* phdr = &km_guest.km_phdr[i];
      if (phdr->p_type == PT_LOAD) {
         load_extent(fileno(e->file), phdr, adjust);
      }
   }
   km_close_elf_file(e);

   if (km_guest.km_interp_vaddr != 0) {
      load_dynlink(km_guest.km_interp_vaddr, km_guest.km_interp_len, adjust);
   }
   km_guest.km_load_adjust = adjust;
   return adjust;
}

/*
 * Open ELF file for KM.
 * Note: This is a place where we need to really validate what we are looking
 * at since. Potentially a bad actor could give us a bad elf file for the guest
 * in an attempt to corrupt KM. We need to prevent that.
 * For now there are bsic tests. 
 */
km_elf_t*
km_open_elf_file(const char *path)
{
   // Open ELF file
   km_elf_t *elf = (km_elf_t *) malloc(sizeof(km_elf_t));
   memset(elf, 0, sizeof(km_elf_t));
   elf->path = path;
   if ((elf->file = fopen(elf->path, "r")) == NULL) {
      km_err(2, "Cannot open %s", path);
   }

   // Read ELF header
   elf->ehdr = (Elf64_Ehdr *) malloc(sizeof(Elf64_Ehdr));
   int nread = fread(elf->ehdr, 1, sizeof(Elf64_Ehdr), elf->file);
   if (nread < sizeof(Elf64_Ehdr)) {
      km_err(2, "Cannot read EHDR %s", path);
   }

   // Validate Elf Header
   unsigned char* wp = &elf->ehdr->e_ident[0];
   if (memcmp(wp, ELFMAG, SELFMAG) != 0) {
      km_errx(2, "EHDR magic number mismatch %s", path);
   }
   if (elf->ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
      km_errx(2, "Not a 64 bit ELF %s", path);
   }
   if (elf->ehdr->e_version != EV_CURRENT) {
      km_errx(2, "Not current ELF version %s", path);
   }

   elf->phdr = malloc(elf->ehdr->e_phentsize * elf->ehdr->e_phnum);
   if (fseek(elf->file, elf->ehdr->e_phoff, SEEK_SET) != 0) {
      km_err(2, "Cannot seek to PHDR %s", path);
   }
   nread = fread(elf->phdr, 1, elf->ehdr->e_phentsize * elf->ehdr->e_phnum, elf->file);
   if (nread < elf->ehdr->e_phentsize * elf->ehdr->e_phnum) {
      km_err(2, "Cannot read PHDR %s", path);
   }

   // Read Section Headers
   elf->shdr = malloc(elf->ehdr->e_shentsize * elf->ehdr->e_shnum);
   if (fseek(elf->file, elf->ehdr->e_shoff, SEEK_SET) != 0) {
      km_err(2, "Cannot seek to SHDR %s", path);
   }
   nread = fread(elf->shdr, 1, elf->ehdr->e_shentsize * elf->ehdr->e_shnum, elf->file);
   if (nread < elf->ehdr->e_shentsize * elf->ehdr->e_shnum) {
      km_err(2, "Cannot read SHDR %s", path);
   }

   // Find Symbol table and String Table Indexes
   elf->symidx = elf->stridx = -1;
   for (int i = 0; i < elf->ehdr->e_shnum; i++) {
      if (elf->shdr[i].sh_type == SHT_SYMTAB) {
         elf->symidx = i;
      }
      if (elf->shdr[i].sh_type == SHT_STRTAB && i != elf->ehdr->e_shstrndx) {
         elf->stridx = i;
      }
   }

   return elf;
}

void
km_close_elf_file(km_elf_t* elf)
{
   if (elf != NULL) {
      if (elf->file != NULL) {
         fclose(elf->file);
         elf->file = NULL;
      }
      if (elf->ehdr != NULL) {
         free(elf->ehdr);
         elf->ehdr = NULL;
      }
      if (elf->phdr != NULL) {
         free(elf->phdr);
         elf->phdr = NULL;
      }
      if (elf->shdr != NULL) {
         free(elf->shdr);
         elf->shdr = NULL;
      }
      free(elf);
   }
}
