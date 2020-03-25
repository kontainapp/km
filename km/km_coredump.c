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

static inline char* km_get_coredump_path()
{
   return coredump_path;
}

/*
 * Write a buffer in KM memory.
 */
static inline void km_core_write_mem(int fd, void* buffer, size_t length, int is_guestmem)
{
   int rc;
   char* cur = buffer;
   size_t remain = length;

   while (remain > 0) {
      if ((rc = write(fd, cur, remain)) == -1) {
         if (errno == EFAULT && is_guestmem) {
            if (lseek(fd, remain, SEEK_CUR) < 0) {
               km_err_msg(errno, "lseek error fd=%d cur=%p remain=0x%lx", fd, cur, remain);
               errx(errno, "exiting...");
            }
            rc = remain;
         } else {
            km_err_msg(errno,
                       "write error - cur=%p remain=0x%lx buffer=%p length=0x%lx\n",
                       cur,
                       remain,
                       buffer,
                       length);
            errx(errno, "exiting...\n");
         }
      }
      remain -= rc;
      cur += rc;
   }
}

static inline void km_core_write(int fd, void* buffer, size_t length)
{
   return km_core_write_mem(fd, buffer, length, 0);
}

static inline void km_core_write_elf_header(int fd, int phnum)
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

static inline void
km_core_write_load_header(int fd, off_t offset, km_gva_t base, size_t size, int flags)
{
   Elf64_Phdr phdr = {};

   km_infox(KM_TRACE_COREDUMP,
            "PT_LOAD: base=0x%lx offset=0x%lx size=0x%lx flags=0x%x",
            base,
            offset,
            size,
            flags);

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

static char*
km_core_dump_region_mapping(km_vcpu_t* vcpu, km_gva_t vaddr, km_gva_t len, km_gva_t offset, char* cur)
{
   uint64_t* fent = (uint64_t*)cur;
   fent[0] = vaddr;
   fent[1] = vaddr + len;
   fent[2] = offset / KM_PAGE_SIZE;
   return (char*)&fent[3];
}

static inline char* km_core_dump_elf_mappings(km_vcpu_t* vcpu, km_payload_t* payload, char* cur)
{
   for (int i = 0; i < payload->km_ehdr.e_phnum; i++) {
      Elf64_Phdr* phdr = &payload->km_phdr[i];
      if (phdr->p_type != PT_LOAD) {
         continue;
      }
      cur = km_core_dump_region_mapping(vcpu,
                                        phdr->p_vaddr + payload->km_load_adjust,
                                        phdr->p_memsz,
                                        phdr->p_offset,
                                        cur);
   }
   return cur;
}

static inline char* km_core_dump_mapping_name(km_vcpu_t* vcpu, char* name, char* cur)
{
   size_t len = strlen(name) + 1;
   strcpy(cur, name);
   cur += len;
   return cur;
}

static inline int km_core_dump_mappings(km_vcpu_t* vcpu, char* buf, size_t length)
{
   char* cur = buf;
   size_t remain = length;
   km_mmap_reg_t* ptr;

   // Pass 1 - Compute size of entry and # of files);

   size_t nfiles = 0;
   size_t notelen = km_mappings_size(vcpu, &nfiles);

   // Pass 2 - Note records
   cur += km_add_note_header(cur, remain, "CORE", NT_FILE, notelen);

   uint64_t* fent = (uint64_t*)cur;
   fent[0] = nfiles;
   fent[1] = KM_PAGE_SIZE;
   cur += 2 * sizeof(*fent);
   cur = km_core_dump_elf_mappings(vcpu, &km_guest, cur);
   if (km_dynlinker.km_filename != NULL) {
      cur = km_core_dump_elf_mappings(vcpu, &km_dynlinker, cur);
   }
   TAILQ_FOREACH (ptr, &machine.mmaps.busy, link) {
      if (ptr->filename == NULL) {
         continue;
      }
      cur = km_core_dump_region_mapping(vcpu, ptr->start, ptr->size, ptr->offset, cur);
   }

   // pass 3 - file names.
   for (int i = 0; i < km_guest.km_ehdr.e_phnum; i++) {
      Elf64_Phdr* phdr = &km_guest.km_phdr[i];
      if (phdr->p_type != PT_LOAD) {
         continue;
      }
      cur = km_core_dump_mapping_name(vcpu, km_guest.km_filename, cur);
   }
   if (km_dynlinker.km_filename != NULL) {
      for (int i = 0; i < km_dynlinker.km_ehdr.e_phnum; i++) {
         Elf64_Phdr* phdr = &km_dynlinker.km_phdr[i];
         if (phdr->p_type != PT_LOAD) {
            continue;
         }
         cur = km_core_dump_mapping_name(vcpu, km_dynlinker.km_filename, cur);
      }
   }
   TAILQ_FOREACH (ptr, &machine.mmaps.busy, link) {
      if (ptr->filename == NULL) {
         continue;
      }
      cur = km_core_dump_mapping_name(vcpu, ptr->filename, cur);
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

static inline int km_core_write_notes(km_vcpu_t* vcpu, int fd, off_t offset, char* buf, size_t size)
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
static inline void km_guestmem_write(int fd, km_gva_t base, size_t length)
{
   km_gva_t current = base;
   size_t remain = length;
   static size_t maxwrite = MIB;

   // Page Align
   off_t off = lseek(fd, 0, SEEK_CUR);
   if (off != roundup(off, KM_PAGE_SIZE)) {
      off = roundup(off, KM_PAGE_SIZE);
      lseek(fd, off, SEEK_SET);
   }
   km_infox(KM_TRACE_COREDUMP, "base=0x%lx length=0x%lx off=0x%lx", base, length, off);
   while (remain > 0) {
      size_t wsz = MIN(remain, maxwrite);

      km_core_write_mem(fd, km_gva_to_kma_nocheck(current), wsz, 1);
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
static inline size_t km_core_notes_length()
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

// Calculates the number of PHDRs in the core file.
static inline int km_core_count_phdrs(km_vcpu_t* vcpu, km_gva_t* endloadp)
{
   km_mmap_reg_t* ptr;
   int phnum = 1;   // 1 for PT_NOTE
   km_gva_t endload = 0;

   // Count up phdrs for mmaps and set offset where data will start.
   for (int i = 0; i < km_guest.km_ehdr.e_phnum; i++) {
      if (km_guest.km_phdr[i].p_type != PT_LOAD) {
         continue;
      }
      km_gva_t pend = km_guest.km_phdr[i].p_vaddr + km_guest.km_phdr[i].p_memsz;
      if (pend > endload) {
         endload = pend;
      }

      phnum++;
   }
   endload += km_guest.km_load_adjust;
   if (km_dynlinker.km_filename != NULL) {
      for (int i = 0; i < km_dynlinker.km_ehdr.e_phnum; i++) {
         if (km_dynlinker.km_phdr[i].p_type != PT_LOAD) {
            continue;
         }
         km_gva_t pend = km_dynlinker.km_phdr[i].p_vaddr + km_dynlinker.km_phdr[i].p_memsz;
         if (pend > endload) {
            endload = pend;
         }

         phnum++;
      }
   }
   TAILQ_FOREACH (ptr, &machine.mmaps.busy, link) {
      if (ptr->protection == PROT_NONE) {
         continue;
      }
      phnum++;
   }

   *endloadp = endload;
   return phnum;
}

/*
 * We add the data upto machine.brk to PT_LOAD for the last loaded ELF segment.
 * This allows snapshot restore to use mmapsince everything is page aligned.
 */
static inline int km_core_last_load_adjust(Elf64_Phdr* phdr, km_gva_t end_load)
{
   if (phdr->p_vaddr + km_guest.km_load_adjust + phdr->p_memsz == end_load) {
      return machine.brk - end_load;
   }
   return 0;
}

static inline void km_core_write_phdrs(km_vcpu_t* vcpu,
                                       int fd,
                                       int phnum,
                                       km_gva_t end_load,
                                       char* notes_buffer,
                                       size_t notes_length,
                                       size_t* offsetp)
{
   km_mmap_reg_t* ptr;

   // write elf header
   km_core_write_elf_header(fd, phnum);
   // Create PT_NOTE in memory and write the header
   *offsetp += km_core_write_notes(vcpu, fd, *offsetp, notes_buffer, notes_length);
   // Write headers for segments from ELF
   for (int i = 0; i < km_guest.km_ehdr.e_phnum; i++) {
      if (km_guest.km_phdr[i].p_type != PT_LOAD) {
         continue;
      }
      size_t write_size = km_guest.km_phdr[i].p_memsz;
      write_size += km_core_last_load_adjust(&km_guest.km_phdr[i], end_load);
      *offsetp = roundup(*offsetp, KM_PAGE_SIZE);
      km_core_write_load_header(fd,
                                *offsetp,
                                km_guest.km_phdr[i].p_vaddr + km_guest.km_load_adjust,
                                write_size,
                                km_guest.km_phdr[i].p_flags);
      *offsetp += write_size;
   }
   if (km_dynlinker.km_filename != NULL) {
      for (int i = 0; i < km_dynlinker.km_ehdr.e_phnum; i++) {
         if (km_dynlinker.km_phdr[i].p_type != PT_LOAD) {
            continue;
         }
         size_t write_size = km_dynlinker.km_phdr[i].p_memsz;
         write_size += km_core_last_load_adjust(&km_dynlinker.km_phdr[i], end_load);
         *offsetp = roundup(*offsetp, KM_PAGE_SIZE);
         km_core_write_load_header(fd,
                                   *offsetp,
                                   km_dynlinker.km_phdr[i].p_vaddr + km_dynlinker.km_load_adjust,
                                   write_size,
                                   km_dynlinker.km_phdr[i].p_flags);
         *offsetp += write_size;
      }
   }
   // Headers for MMAPs
   TAILQ_FOREACH (ptr, &machine.mmaps.busy, link) {
      // translate mmap prot to elf access flags.
      if (ptr->protection == PROT_NONE) {
         continue;
      }
      static uint8_t mmap_to_elf_flags[8] =
          {0, PF_R, PF_W, (PF_R | PF_W), PF_X, (PF_R | PF_X), (PF_W | PF_X), (PF_R | PF_W | PF_X)};

      *offsetp = roundup(*offsetp, KM_PAGE_SIZE);
      km_core_write_load_header(fd,
                                *offsetp,
                                ptr->start,
                                ptr->size,
                                mmap_to_elf_flags[ptr->protection & 0x7]);
      *offsetp += ptr->size;
   }
}

/*
 * Drop a core file containing the guest image.
 */
void km_dump_core(km_vcpu_t* vcpu, x86_interrupt_frame_t* iframe)
{
   char* core_path = km_get_coredump_path();
   int fd;
   size_t offset;   // Data offset
   km_mmap_reg_t* ptr;
   char* notes_buffer;
   size_t notes_length = km_core_notes_length();
   km_gva_t end_load = 0;
   int phnum = km_core_count_phdrs(vcpu, &end_load);

   if ((fd = open(core_path, O_RDWR | O_CREAT | O_TRUNC, 0666)) < 0) {
      km_err_msg(errno, "Cannot open corefile '%s'", core_path);
      errx(2, "exiting...");
   }
   warnx("Write coredump to '%s'", core_path);

   if ((notes_buffer = (char*)calloc(1, notes_length)) == NULL) {
      km_err_msg(errno, "cannot allocate notes buffer");
      errx(2, "exiting...\n");
   }
   memset(notes_buffer, 0, notes_length);
   offset = sizeof(Elf64_Ehdr) + phnum * sizeof(Elf64_Phdr);

   km_core_write_phdrs(vcpu, fd, phnum, end_load, notes_buffer, notes_length, &offset);

   // Write the actual data.
   km_core_write(fd, notes_buffer, notes_length);
   km_infox(KM_TRACE_COREDUMP, "Dump executable");
   for (int i = 0; i < km_guest.km_ehdr.e_phnum; i++) {
      if (km_guest.km_phdr[i].p_type != PT_LOAD) {
         continue;
      }
      size_t write_size = km_guest.km_phdr[i].p_memsz;
      write_size += km_core_last_load_adjust(&km_guest.km_phdr[i], end_load);
      km_guestmem_write(fd, km_guest.km_phdr[i].p_vaddr + km_guest.km_load_adjust, write_size);
   }
   if (km_dynlinker.km_filename != NULL) {
      km_infox(KM_TRACE_COREDUMP, "Dump dynlinker");
      for (int i = 0; i < km_dynlinker.km_ehdr.e_phnum; i++) {
         if (km_dynlinker.km_phdr[i].p_type != PT_LOAD) {
            continue;
         }
         size_t write_size = km_dynlinker.km_phdr[i].p_memsz;
         write_size += km_core_last_load_adjust(&km_dynlinker.km_phdr[i], end_load);
         km_guestmem_write(fd, km_dynlinker.km_phdr[i].p_vaddr + km_dynlinker.km_load_adjust, write_size);
      }
   }
   km_infox(KM_TRACE_COREDUMP, "Dump mmaps");
   TAILQ_FOREACH (ptr, &machine.mmaps.busy, link) {
      if (ptr->protection == PROT_NONE) {
         continue;
      }
      km_kma_t start = km_gva_to_kma_nocheck(ptr->start);
      // make sure we can read the mapped memory (e.g. it can be EXEC only)
      if (ptr->km_flags.km_mmap_part_of_monitor == 0 && (ptr->protection & PROT_READ) != PROT_READ) {
         if (mprotect(start, ptr->size, ptr->protection | PROT_READ) != 0) {
            km_err_msg(0, "failed to make %p,0x%lx readable for dump", start, ptr->size);
            errx(2, "exiting...");
         }
      }
      km_guestmem_write(fd, ptr->start, ptr->size);
      // recover protection, in case it's a live coredump and we are not exiting yet
      if (ptr->km_flags.km_mmap_part_of_monitor == 0 && (ptr->protection & PROT_READ) != PROT_READ &&
          mprotect(start, ptr->size, ptr->protection) != 0) {
         km_err_msg(errno, "failed to set %p,0x%lx prot to 0x%x", start, ptr->size, ptr->protection);
         errx(2, "exiting...");
      }
   }

   free(notes_buffer);
   (void)close(fd);
}
