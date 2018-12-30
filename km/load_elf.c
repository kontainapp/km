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
#include <gelf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static void my_pread(int fd, void *buf, size_t count, off_t offset)
{
   int i, rc;

   for (i = 0, rc = 0; i < count; i += rc) {
      // rc == 0 means EOF which to us means error in the format
      if ((rc = pread(fd, buf + i, count - i, offset + i)) <= 0 &&
          rc != EINTR) {
         err(2, "error reading elf");
      }
   }
}

/*
 * Read elf executable file and initialize mem with the content of the program
 * segments. Set entry point.
 * All errors are fatal.
 */
int load_elf(const char *file, void *mem, uint64_t *entry)
{
   int fd;
   Elf *e;
   GElf_Ehdr ehdr;
   GElf_Phdr phdr;
   int i, pr;
   void *addr;

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
   if (gelf_getehdr(e, &ehdr) == NULL) {
      errx(2, "gelf_getehrd %s", elf_errmsg(-1));
   }
   *entry = ehdr.e_entry;
   for (i = 0; i < ehdr.e_phnum; i++) {
      if (gelf_getphdr(e, i, &phdr) == NULL) {
         errx(2, "gelf_getphrd %i, %s", i, elf_errmsg(-1));
      }
      if (phdr.p_type != PT_LOAD) {
         continue;
      }
      /*
       * TODO: check we fit in memory
       */
      addr = mem + phdr.p_paddr;
      my_pread(fd, addr, phdr.p_filesz, phdr.p_offset);
      memset(addr + phdr.p_filesz, 0, phdr.p_memsz - phdr.p_filesz);
      pr = 0;
      if (phdr.p_flags & PF_R) {
         pr |= PROT_READ;
      }
      if (phdr.p_flags & PF_W) {
         pr |= PROT_WRITE;
      }
      if (phdr.p_flags & PF_X) {
         pr |= PROT_EXEC;
      }
      if (mprotect(addr, phdr.p_memsz, pr) < 0) {
         err(2, "failed to set guest memory protection");
      }
   }
   (void)elf_end(e);
   (void)close(fd);
   return 0;
}