/*
 * Copyright © 2019 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Support writing coredump files for failing guest.
 *
 * Linux source files for reference:
 *  include/uapi/linux/elfcore.h
 *  fs/binfmt_elf.c
 *
 * TODO: Buffer management for PT_NOTES is vulnerable to overruns. Need to fix.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/procfs.h>

#include "km.h"
#include "km_coredump.h"
#include "km_filesys.h"
#include "km_mem.h"

// TODO: Need to figure out where the corefile default should go.
static char* coredump_path = "./kmcore";

void km_set_coredump_path(char* path)
{
   km_infox(KM_TRACE_COREDUMP, "Setting coredump path to %s", path);
   coredump_path = path;
}

char* km_get_coredump_path()
{
   return coredump_path;
}

/*
 * Write a buffer in KM memory.
 */
void km_core_write(int fd, void* buffer, size_t length)
{
   int rc;

   if ((rc = write(fd, buffer, length)) == -1) {
      errx(2, "%s - write error - errno=%d [%s] buffer=%p\n", __FUNCTION__, errno, strerror(errno), buffer);
   }
   if (rc != length) {
      errx(2, "%s - short write: expect: %ld got:%d", __FUNCTION__, length, rc);
   }
}

void km_core_write_elf_header(int fd, int phnum)
{
   Elf64_Ehdr ehdr = {};

   km_infox(KM_TRACE_COREDUMP, "CORE file: phnum=%d", phnum);
   // Format the Elf Header
   memcpy(ehdr.e_ident, km_guest.km_ehdr.e_ident, sizeof(ehdr.e_ident));
   ehdr.e_type = ET_CORE;
   ehdr.e_machine = km_guest.km_ehdr.e_machine;
   ehdr.e_version = km_guest.km_ehdr.e_version;
   ehdr.e_entry = km_guest.km_ehdr.e_entry;
   ehdr.e_phoff = sizeof(Elf64_Ehdr);
   ehdr.e_shoff = 0;
   ehdr.e_flags = 0;
   ehdr.e_ehsize = sizeof(Elf64_Ehdr);
   ehdr.e_phentsize = sizeof(Elf64_Phdr);
   ehdr.e_phnum = phnum;
   ehdr.e_shentsize = 0;
   ehdr.e_shnum = 0;
   ehdr.e_shstrndx = 0;

   km_core_write(fd, &ehdr, sizeof(Elf64_Ehdr));
}

void km_core_write_load_header(int fd, off_t offset, km_gva_t base, size_t size, int flags)
{
   Elf64_Phdr phdr = {};

   km_infox(KM_TRACE_COREDUMP, "PT_LOAD: base=0x%lx size=0x%lx flags=0x%x", base, offset, flags);

   phdr.p_type = PT_LOAD;
   phdr.p_offset = offset;
   phdr.p_vaddr = base;
   phdr.p_paddr = 0;
   phdr.p_filesz = size;
   phdr.p_memsz = size;
   phdr.p_flags = flags;

   km_core_write(fd, &phdr, sizeof(Elf64_Phdr));
}

// TODO padding?
static int km_add_note_header(char* buf, size_t length, char* owner, int type, size_t descsz)
{
   Elf64_Nhdr* nhdr;
   char* cur = buf;
   size_t ownersz = strlen(owner);

   nhdr = (Elf64_Nhdr*)cur;
   nhdr->n_type = type;
   nhdr->n_namesz = ownersz;
   nhdr->n_descsz = descsz;
   cur += sizeof(Elf64_Nhdr);
   strcpy(cur, owner);
   cur += ownersz;
   return cur - buf;
}

static inline int km_make_dump_prstatus(km_vcpu_t* vcpu, char* buf, size_t length)
{
   struct elf_prstatus pr = {};
   char* cur = buf;
   size_t remain = length;

   km_read_registers(vcpu);
   km_read_sregisters(vcpu);

   cur += km_add_note_header(cur, remain, "CORE", NT_PRSTATUS, sizeof(struct elf_prstatus));

   // TODO: Fill in the rest of elf_prstatus.
   pr.pr_pid = km_vcpu_get_tid(vcpu);
   // Found these assignments in Linux source arch/x86/include/asm/elf.h
   pr.pr_reg[0] = vcpu->regs.r15;
   pr.pr_reg[1] = vcpu->regs.r14;
   pr.pr_reg[2] = vcpu->regs.r13;
   pr.pr_reg[3] = vcpu->regs.r12;
   pr.pr_reg[4] = vcpu->regs.rbp;
   pr.pr_reg[5] = vcpu->regs.rbx;
   pr.pr_reg[6] = vcpu->regs.r11;
   pr.pr_reg[7] = vcpu->regs.r10;
   pr.pr_reg[8] = vcpu->regs.r9;
   pr.pr_reg[9] = vcpu->regs.r8;
   pr.pr_reg[10] = vcpu->regs.rax;
   pr.pr_reg[11] = vcpu->regs.rcx;
   pr.pr_reg[12] = vcpu->regs.rdx;
   pr.pr_reg[13] = vcpu->regs.rsi;
   pr.pr_reg[14] = vcpu->regs.rdi;
   pr.pr_reg[15] = vcpu->regs.rax;   // orig_ax?
   pr.pr_reg[16] = vcpu->regs.rip;
   pr.pr_reg[17] = vcpu->sregs.cs.base;
   pr.pr_reg[18] = vcpu->regs.rflags;
   pr.pr_reg[19] = vcpu->regs.rsp;
   pr.pr_reg[20] = vcpu->sregs.ss.base;
   pr.pr_reg[21] = vcpu->sregs.fs.base;
   pr.pr_reg[22] = vcpu->sregs.gs.base;
   pr.pr_reg[23] = vcpu->sregs.ds.base;
   pr.pr_reg[24] = vcpu->sregs.es.base;
   pr.pr_reg[25] = vcpu->sregs.fs.base;
   pr.pr_reg[26] = vcpu->sregs.gs.base;

   memcpy(cur, &pr, sizeof(struct elf_prstatus));
   cur += sizeof(struct elf_prstatus);
   remain -= sizeof(struct elf_prstatus);

   return roundup(cur - buf, 4);
}

typedef struct {
   km_vcpu_t* pr_vcpu;   // 'current', already output. skip
   char* pr_cur;
   size_t pr_remain;
} km_core_dump_prstatus_t;

static int km_core_dump_threads(km_vcpu_t* vcpu, uint64_t arg)
{
   km_core_dump_prstatus_t* ctx = (km_core_dump_prstatus_t*)arg;
   if (vcpu == ctx->pr_vcpu) {
      return 0;
   }
   size_t ret;
   ret = km_make_dump_prstatus(vcpu, ctx->pr_cur, ctx->pr_remain);
   ctx->pr_cur += ret;
   ctx->pr_remain -= ret;
   return ret;
}

/*
 * Format of NT_FILE note:
 *
 * long count     -- how many files are mapped
 * long page_size -- units for file_ofs
 * array of [COUNT] elements of
 *   long start
 *   long end
 *   long file_ofs
 * followed by COUNT filenames in ASCII: "FILE1" NUL "FILE2" NUL...
 */
static size_t km_mappings_size(km_vcpu_t* vcpu, size_t* nfilesp)
{
   km_mmap_reg_t* ptr;
   size_t notelen = 2 * sizeof(uint64_t);
   size_t nfiles = 0;

   for (int i = 0; i < km_guest.km_ehdr.e_phnum; i++) {
      Elf64_Phdr* phdr = &km_guest.km_phdr[i];
      if (phdr->p_type != PT_LOAD) {
         continue;
      }
      notelen += 3 * sizeof(uint64_t) + strlen(km_guest.km_filename) + 1;
      nfiles++;
   }
   if (km_dynlinker.km_filename != NULL) {
      for (int i = 0; i < km_dynlinker.km_ehdr.e_phnum; i++) {
         Elf64_Phdr* phdr = &km_dynlinker.km_phdr[i];
         if (phdr->p_type != PT_LOAD) {
            continue;
         }
         notelen += 3 * sizeof(uint64_t) + strlen(km_dynlinker.km_filename) + 1;
         nfiles++;
      }
   }
   TAILQ_FOREACH (ptr, &machine.mmaps.busy, link) {
      if (ptr->filename == NULL) {
         continue;
      }
      notelen += 3 * sizeof(uint64_t) + strlen(ptr->filename) + 1;
      nfiles++;
   }
   if (nfilesp != NULL) {
      *nfilesp = nfiles;
   }
   return notelen;
}

static inline int km_core_dump_mappings(km_vcpu_t* vcpu, char* buf, size_t length)
{
   char* cur = buf;
   size_t remain = length;
   km_mmap_reg_t* ptr;

   // Pass 1 - Compute size of entry and # of files);
   size_t notelen = 2 * sizeof(uint64_t);
   size_t nfiles = 0;
   for (int i = 0; i < km_guest.km_ehdr.e_phnum; i++) {
      Elf64_Phdr* phdr = &km_guest.km_phdr[i];
      if (phdr->p_type != PT_LOAD) {
         continue;
      }
      notelen += 3 * sizeof(uint64_t) + strlen(km_guest.km_filename) + 1;
      nfiles++;
   }
   if (km_dynlinker.km_filename != NULL) {
      for (int i = 0; i < km_dynlinker.km_ehdr.e_phnum; i++) {
         Elf64_Phdr* phdr = &km_dynlinker.km_phdr[i];
         if (phdr->p_type != PT_LOAD) {
            continue;
         }
         notelen += 3 * sizeof(uint64_t) + strlen(km_dynlinker.km_filename) + 1;
         nfiles++;
      }
   }
   TAILQ_FOREACH (ptr, &machine.mmaps.busy, link) {
      if (ptr->filename == NULL) {
         continue;
      }
      notelen += 3 * sizeof(uint64_t) + strlen(ptr->filename) + 1;
      nfiles++;
   }

   cur += km_add_note_header(cur, remain, "CORE", NT_FILE, notelen);

   // Pass 2 - Note records
   uint64_t* fent = (uint64_t*)cur;
   fent[0] = nfiles;
   fent[1] = KM_PAGE_SIZE;
   cur += 2 * sizeof(*fent);
   for (int i = 0; i < km_guest.km_ehdr.e_phnum; i++) {
      Elf64_Phdr* phdr = &km_guest.km_phdr[i];
      if (phdr->p_type != PT_LOAD) {
         continue;
      }
      fent = (uint64_t*)cur;
      fent[0] = phdr->p_vaddr + km_guest.km_load_adjust;
      fent[1] = phdr->p_vaddr + km_guest.km_load_adjust + phdr->p_memsz;
      fent[2] = phdr->p_offset / KM_PAGE_SIZE;
      cur += 3 * sizeof(*fent);
      remain -= 3 * sizeof(*fent);
   }
   if (km_dynlinker.km_filename != NULL) {
      for (int i = 0; i < km_dynlinker.km_ehdr.e_phnum; i++) {
         Elf64_Phdr* phdr = &km_dynlinker.km_phdr[i];
         if (phdr->p_type != PT_LOAD) {
            continue;
         }
         fent = (uint64_t*)cur;
         fent[0] = phdr->p_vaddr + km_dynlinker.km_load_adjust;
         fent[1] = phdr->p_vaddr + km_dynlinker.km_load_adjust + phdr->p_memsz;
         fent[2] = phdr->p_offset / KM_PAGE_SIZE;
         cur += 3 * sizeof(*fent);
         remain -= 3 * sizeof(*fent);
      }
   }

   TAILQ_FOREACH (ptr, &machine.mmaps.busy, link) {
      if (ptr->filename == NULL) {
         continue;
      }
      fent = (uint64_t*)cur;
      fent[0] = ptr->start;
      fent[1] = ptr->start + ptr->size;
      fent[2] = ptr->offset / KM_PAGE_SIZE;
      cur += 3 * sizeof(*fent);
      remain -= 3 * sizeof(*fent);
   }

   // pass 3 - file names.
   for (int i = 0; i < km_guest.km_ehdr.e_phnum; i++) {
      Elf64_Phdr* phdr = &km_guest.km_phdr[i];
      if (phdr->p_type != PT_LOAD) {
         continue;
      }
      char* name = km_guest.km_filename;
      size_t len = strlen(name) + 1;
      strcpy(cur, name);
      cur += len;
   }
   if (km_dynlinker.km_filename != NULL) {
      for (int i = 0; i < km_dynlinker.km_ehdr.e_phnum; i++) {
         Elf64_Phdr* phdr = &km_dynlinker.km_phdr[i];
         if (phdr->p_type != PT_LOAD) {
            continue;
         }
         char* name = km_dynlinker.km_filename;
         size_t len = strlen(name) + 1;
         strcpy(cur, name);
         cur += len;
      }
   }
   TAILQ_FOREACH (ptr, &machine.mmaps.busy, link) {
      if (ptr->filename == NULL) {
         continue;
      }
      char* name = ptr->filename;
      size_t len = strlen(name) + 1;
      strcpy(cur, name);
      cur += len;
   }
   return roundup(cur - buf, 4);
}

static inline int km_core_dump_auxv(km_vcpu_t* vcpu, char* buf, size_t length)
{
   char* cur = buf;
   size_t remain = length;

   cur += km_add_note_header(cur, remain, "CORE", NT_AUXV, machine.auxv_size);
   memcpy(cur, machine.auxv, machine.auxv_size);
   cur += machine.auxv_size;
   return roundup(cur - buf, 4);
}

int km_core_write_notes(km_vcpu_t* vcpu, int fd, off_t offset, char* buf, size_t size)
{
   Elf64_Phdr phdr = {};

   char* cur = buf;
   size_t remain = size;
   size_t ret;

   /*
    * NT_PRSTATUS (prstatus structure)
    * There is one NT_PRSTATUS for each running thread (vcpu) in the guest.
    * GDB interprts the first prstatus as it's initial current thread, so
    * it is important the the failing thread be the first one in the list.
    */
   ret = km_make_dump_prstatus(vcpu, cur, remain);
   cur += ret;
   remain -= ret;

   km_core_dump_prstatus_t ctx = {.pr_vcpu = vcpu, .pr_cur = cur, .pr_remain = remain};
   km_vcpu_apply_all(km_core_dump_threads, (uint64_t)&ctx);
   cur = ctx.pr_cur;
   remain = ctx.pr_remain;

   //  NT_FILE (mapped files)
   ret = km_core_dump_mappings(vcpu, cur, remain);
   cur += ret;
   remain -= ret;

   //  NT_AUXV (auxiliary vector)
   ret = km_core_dump_auxv(vcpu, cur, remain);
   cur += ret;
   remain -= ret;

   // TODO: Other notes sections in real core files.
   //  NT_PRPSINFO (prpsinfo structure)
   //  NT_SIGINFO (siginfo_t data)
   //  NT_FPREGSET (floating point registers)
   //  NT_X86_XSTATE (X86 XSAVE extended state)

   phdr.p_type = PT_NOTE;
   phdr.p_offset = offset;
   phdr.p_vaddr = 0;
   phdr.p_paddr = 0;
   phdr.p_filesz = cur - buf;
   phdr.p_memsz = 0;
   phdr.p_flags = 0;
   km_core_write(fd, &phdr, sizeof(Elf64_Phdr));

   // return cur - buf;
   return size;
}

/*
 * Write a contiguous area in guest memory. Since write can
 * be very large, break it up into reasonably sized pieces (1MB).
 * We assume we'll always be able to write 1MB.
 */
void km_guestmem_write(int fd, km_gva_t base, size_t length)
{
   km_gva_t current = base;
   size_t remain = length;
   static size_t maxwrite = MIB;

   while (remain > 0) {
      size_t wsz = MIN(remain, maxwrite);

      km_core_write(fd, km_gva_to_kma_nocheck(current), wsz);
      current += wsz;
      remain -= wsz;
   }
}

static int km_count_vcpu(km_vcpu_t* vcpu, uint64_t unused)
{
   return 1;
}

/*
 * Returns buffer allocation size for core PT_NOTES section based on the
 * number of active vcpu's (threads).
 */
size_t km_core_notes_length()
{
   int nvcpu = km_vcpu_apply_all(km_count_vcpu, 0);
   /*
    * nvcpu is incremented because the current vcpu is wrtten twice.
    * At the beginning ats the default and again in position.
    */
   size_t alloclen = sizeof(Elf64_Nhdr) + ((nvcpu + 1) * sizeof(struct elf_prstatus));
   alloclen += km_mappings_size(NULL, NULL) + sizeof(Elf64_Nhdr);
   alloclen += machine.auxv_size + sizeof(Elf64_Nhdr);

   return roundup(alloclen, KM_PAGE_SIZE);
}