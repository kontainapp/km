/*
 * Copyright 2021 Kontain Inc.
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

/*
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
#include "km_guest.h"
#include "km_mem.h"
#include "km_signal.h"

// TODO: Need to figure out where the corefile and snapshotdefault should go.
static char* coredump_path = "./kmcore";

void km_set_coredump_path(char* path)
{
   km_infox(KM_TRACE_COREDUMP, "Setting coredump path to %s", path);
   if ((coredump_path = strdup(path)) == NULL) {
      km_err(1, "Failed to alloc memory for coredump path");
   }
}

char* km_get_coredump_path()
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
               km_err(errno, "lseek error fd=%d cur=%p remain=0x%lx, exiting", fd, cur, remain);
            }
            rc = remain;
         } else {
            km_err(errno,
                   "write error - cur=%p remain=0x%lx buffer=%p length=0x%lx, exiting",
                   cur,
                   remain,
                   buffer,
                   length);
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

size_t km_note_header_size(char* owner)
{
   return sizeof(Elf64_Nhdr) + strlen(owner);
}

// TODO padding?
int km_add_note_header(char* buf, size_t length, char* owner, int type, size_t descsz)
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
} km_core_list_context_t;

static int km_core_dump_threads(km_vcpu_t* vcpu, void* arg)
{
   km_core_list_context_t* ctx = arg;
   if (vcpu == ctx->pr_vcpu) {
      return 0;
   }
   size_t ret;
   ret = km_make_dump_prstatus(vcpu, ctx->pr_cur, ctx->pr_remain);
   ctx->pr_cur += ret;
   ctx->pr_remain -= ret;
   return ret;
}

// Dump KM specific data
static inline int km_core_dump_vcpu(km_vcpu_t* vcpu, void* arg)
{
   struct km_nt_vcpu vnote = {.vcpu_id = vcpu->vcpu_id,
                              .stack_top = vcpu->stack_top,
                              .guest_thr = vcpu->guest_thr,
                              .set_child_tid = vcpu->set_child_tid,
                              .clear_child_tid = vcpu->clear_child_tid,
                              .sigaltstack_sp = (Elf64_Addr)vcpu->sigaltstack.ss_sp,
                              .sigaltstack_flags = vcpu->sigaltstack.ss_flags,
                              .sigaltstack_size = vcpu->sigaltstack.ss_size,
                              .mapself_base = vcpu->mapself_base,
                              .mapself_size = vcpu->mapself_size,
                              .hcarg = (Elf64_Addr)km_hcargs[HC_ARGS_INDEX(vcpu->vcpu_id)],
                              .hypercall = vcpu->hypercall,
                              .restart = vcpu->restart,
                              .fp_format = km_vmdriver_fp_format(vcpu)};

   km_core_list_context_t* ctx = arg;
   char* cur = ctx->pr_cur;
   size_t remain = ctx->pr_remain;
   cur += km_add_note_header(cur,
                             remain,
                             KM_NT_NAME,
                             NT_KM_VCPU,
                             sizeof(struct km_nt_vcpu) + km_vmdriver_fpstate_size());
   struct km_nt_vcpu* sav = (struct km_nt_vcpu*)cur;
   memcpy(cur, &vnote, sizeof(struct km_nt_vcpu));
   cur += sizeof(struct km_nt_vcpu);

   // Add floating point state
   if (km_vmdriver_save_fpstate(vcpu, cur, km_vmdriver_fp_format(vcpu), 1) < 0) {
      km_warnx("Error saving FP state");
      // Fixup to indicate no FP state
      sav->fp_format = NT_KM_VCPU_FPDATA_NONE;
   }
   cur += km_vmdriver_fpstate_size();

   size_t ret = cur - ctx->pr_cur;
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

static inline char* km_core_dump_mapping_name(km_vcpu_t* vcpu, const char* name, char* cur)
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

/*
 * Writes the PT_NOTE record for a KM payload record (km_guest or km_dynlinker)
 */
static inline int km_core_dump_payload_note(km_payload_t* payload, int tag, char* buf, size_t length)
{
   char* cur = buf;
   size_t remain = length;

   cur += km_add_note_header(cur,
                             remain,
                             KM_NT_NAME,
                             tag,
                             sizeof(km_nt_guest_t) + payload->km_ehdr.e_phnum * sizeof(Elf64_Phdr) +
                                 strlen(payload->km_filename) + 1);
   km_nt_guest_t* guest = (km_nt_guest_t*)cur;
   guest->load_adjust = payload->km_load_adjust;
   guest->ehdr = payload->km_ehdr;
   cur += sizeof(km_nt_guest_t);
   memcpy(cur, payload->km_phdr, payload->km_ehdr.e_phnum * sizeof(Elf64_Phdr));
   cur += payload->km_ehdr.e_phnum * sizeof(Elf64_Phdr);
   strcpy(cur, payload->km_filename);
   cur += km_nt_file_padded_size(payload->km_filename);

   return roundup(cur - buf, 4);
}

static inline int km_core_write_notes(km_vcpu_t* vcpu,
                                      int fd,
                                      char* label,
                                      char* description,
                                      off_t offset,
                                      char* buf,
                                      size_t size,
                                      km_coredump_type_t dumptype)
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
   if (vcpu != NULL) {
      ret = km_make_dump_prstatus(vcpu, cur, remain);
      cur += ret;
      remain -= ret;
   }

   km_core_list_context_t ctx = {.pr_vcpu = vcpu, .pr_cur = cur, .pr_remain = remain};
   km_vcpu_apply_all(km_core_dump_threads, &ctx);
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

   // NT_KM_PROC - KM specific
   ctx.pr_vcpu = NULL;
   ctx.pr_cur = cur;
   ctx.pr_remain = remain;
   ret = km_vcpu_apply_all(km_core_dump_vcpu, &ctx);
   cur = ctx.pr_cur;
   remain = ctx.pr_remain;

   ret = km_core_dump_payload_note(&km_guest, NT_KM_GUEST, cur, remain);
   cur += ret;
   remain -= ret;
   if (km_dynlinker.km_filename != NULL) {
      ret = km_core_dump_payload_note(&km_dynlinker, NT_KM_DYNLINKER, cur, remain);
      cur += ret;
      remain -= ret;
   }

   /*
    * Since snapshot causes an exit() call when unsnapshotable state is encountered,
    * we avoid writing open file state for coredumps where it seems likely we would
    * see things that are not snapshotable (like established network connections).
    */
   if (dumptype == KM_DO_SNAP) {
      ret = km_fs_core_notes_write(cur, remain);
      cur += ret;
      remain -= ret;
   }

   ret = km_sig_core_notes_write(cur, remain);
   cur += ret;
   remain -= ret;

   /*
    * Add KM monitor info
    */
   size_t label_sz = (label != NULL) ? strlen(label) + 1 : 0;
   size_t description_sz = (description != NULL) ? strlen(description) + 1 : 0;
   cur += km_add_note_header(cur,
                             remain,
                             KM_NT_NAME,
                             NT_KM_MONITOR,
                             sizeof(km_nt_monitor_t) + label_sz + description_sz);
   remain -= sizeof(km_nt_monitor_t);
   km_nt_monitor_t* monitor = (km_nt_monitor_t*)cur;
   *monitor = (km_nt_monitor_t){.monitor_type = KM_NT_MONITOR_TYPE_KVM,
                                .label_length = label_sz,
                                .description_length = description_sz};
   if (machine.vm_type == VM_TYPE_KKM) {
      monitor->monitor_type = KM_NT_MONITOR_TYPE_KKM;
   }
   cur += sizeof(km_nt_monitor_t);
   remain -= sizeof(km_nt_monitor_t);
   if (label != NULL) {
      strcpy(cur, label);
      cur += strlen(label) + 1;
      remain -= strlen(label) + 1;
   }
   if (description != NULL) {
      strcpy(cur, description);
      cur += strlen(description) + 1;
      remain -= strlen(description) + 1;
   }

   assert(cur <= buf + size);
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
 * We assume we'll always be able to write 1MB. Always write whole
 * pages. This won't hurt since we seek to page boundaries.
 */
static inline void km_guestmem_write(int fd, km_gva_t base, size_t length)
{
   km_gva_t current = base;
   // roundup here to catch the whole page.
   size_t remain = roundup(length, KM_PAGE_SIZE);
   static size_t maxwrite = MIB;

   // Page Align
   off_t off = lseek(fd, 0, SEEK_CUR);
   if (off != roundup(off, KM_PAGE_SIZE)) {
      off = roundup(off, KM_PAGE_SIZE);
      lseek(fd, off, SEEK_SET);
   }
   km_infox(KM_TRACE_COREDUMP, "base=0x%lx length=0x%lx off=0x%lx", base, remain, off);
   while (remain > 0) {
      size_t wsz = MIN(remain, maxwrite);

      km_core_write_mem(fd, km_gva_to_kma_nocheck(current), wsz, 1);
      current += wsz;
      remain -= wsz;
   }
}

/*
 * Returns buffer allocation size for core PT_NOTES section based on the
 * number of active vcpu's (threads).
 */
static inline size_t km_core_notes_length(km_vcpu_t* vcpu, char* label, char* description)
{
   int nvcpu = km_vcpu_run_cnt();
   int nvcpu_inc = (vcpu == NULL) ? 0 : 1;
   /*
    * nvcpu is incremented because the current vcpu is wrtten twice.
    * At the beginning ats the default and again in position.
    */
   size_t alloclen =
       (km_note_header_size(KM_NT_NAME) + sizeof(struct elf_prstatus)) * (nvcpu + nvcpu_inc);
   alloclen += km_mappings_size(NULL, NULL) + strlen(KM_NT_NAME) + sizeof(Elf64_Nhdr);
   alloclen += machine.auxv_size + km_note_header_size(KM_NT_NAME);
   alloclen += sizeof(km_nt_monitor_t) + km_note_header_size(KM_NT_NAME);
   if (label != NULL) {
      alloclen += strlen(label) + 1;
   }
   if (description != NULL) {
      alloclen += strlen(description) + 1;
   }

   // Kontain specific per CPU info (for snapshot restore)
   alloclen +=
       (km_note_header_size(KM_NT_NAME) + sizeof(struct km_nt_vcpu) + km_vmdriver_fpstate_size()) *
       nvcpu;

   // Kontain specific guest info(for snapshot restore)
   alloclen += km_note_header_size(KM_NT_NAME) + sizeof(km_nt_guest_t) +
               km_guest.km_ehdr.e_phnum * sizeof(Elf64_Phdr) +
               km_nt_file_padded_size(km_guest.km_filename);
   if (km_dynlinker.km_filename != NULL) {
      alloclen += km_note_header_size(KM_NT_NAME) + sizeof(km_nt_guest_t) +
                  km_dynlinker.km_ehdr.e_phnum * sizeof(Elf64_Phdr) +
                  km_nt_file_padded_size(km_dynlinker.km_filename);
   }

   alloclen += km_fs_core_notes_length();
   alloclen += km_sig_core_notes_length();

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

static inline size_t
km_core_write_payload_phdr(km_payload_t* payload, km_gva_t end_load, int fd, size_t offset)
{
   for (int i = 0; i < payload->km_ehdr.e_phnum; i++) {
      Elf64_Phdr* phdr = &payload->km_phdr[i];
      if (phdr->p_type != PT_LOAD) {
         continue;
      }
      size_t write_size = phdr->p_memsz;
      write_size += km_core_last_load_adjust(phdr, end_load);
      size_t extra = phdr->p_vaddr - rounddown(phdr->p_vaddr, KM_PAGE_SIZE);
      offset = roundup(offset, KM_PAGE_SIZE);
      km_core_write_load_header(fd,
                                offset,
                                rounddown(phdr->p_vaddr + payload->km_load_adjust, KM_PAGE_SIZE),
                                write_size + extra,
                                phdr->p_flags);
      offset += write_size + extra;
   }
   return offset;
}

static inline void km_core_write_phdrs(km_vcpu_t* vcpu,
                                       int fd,
                                       int phnum,
                                       km_gva_t end_load,
                                       char* notes_buffer,
                                       size_t notes_length,
                                       char* label,
                                       char* description,
                                       size_t* offsetp,
                                       km_coredump_type_t dumptype)
{
   km_mmap_reg_t* ptr;

   // write elf header
   km_core_write_elf_header(fd, phnum);
   // Create PT_NOTE in memory and write the header
   *offsetp +=
       km_core_write_notes(vcpu, fd, label, description, *offsetp, notes_buffer, notes_length, dumptype);
   // Write headers for segments from ELF
   *offsetp = km_core_write_payload_phdr(&km_guest, end_load, fd, *offsetp);
   if (km_dynlinker.km_filename != NULL) {
      *offsetp = km_core_write_payload_phdr(&km_dynlinker, end_load, fd, *offsetp);
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
 * Verify that a snapshot is possible.  We only check for active interval timers
 * at this time.  We don't snapshot interval timers yet, so if there are active timers
 * snapshot is not possible.
 * Returns:
 *  0 - snapshot can proceed
 *  != 0 - snapshot not allowed
 */
static int km_snapshot_ok(void)
{
   int itimer_types[] = {ITIMER_REAL, ITIMER_VIRTUAL, ITIMER_PROF};
   int i;

   for (i = 0; i < sizeof(itimer_types) / sizeof(int); i++) {
      struct itimerval val;
      if (getitimer(itimer_types[i], &val) < 0) {
         km_warn("getitimer type %d failed\n", itimer_types[i]);
         return 1;
      }
      if (val.it_interval.tv_sec != 0 || val.it_interval.tv_usec != 0 || val.it_value.tv_sec != 0 ||
          val.it_value.tv_usec != 0) {
         km_infox(KM_TRACE_COREDUMP,
                  "can't snapshort: timer %d, it_interval %ld.%06lu, it_value %lu.%06lu",
                  itimer_types[i],
                  val.it_interval.tv_sec,
                  val.it_interval.tv_usec,
                  val.it_value.tv_sec,
                  val.it_value.tv_usec);
         return 1;
      }
   }
   return 0;
}

/*
 * Drop a core file containing the guest image.
 */
void km_dump_core(char* core_path,
                  km_vcpu_t* vcpu,
                  x86_interrupt_frame_t* iframe,
                  char* label,
                  char* description,
                  km_coredump_type_t dumptype)
{
   // char* core_path = km_get_coredump_path();
   int fd;
   size_t offset;   // Data offset
   km_mmap_reg_t* ptr;
   char* notes_buffer;
   size_t notes_length = km_core_notes_length(vcpu, label, description);
   km_gva_t end_load = 0;
   int phnum = km_core_count_phdrs(vcpu, &end_load);

   if ((fd = open(core_path, O_RDWR | O_CREAT | O_TRUNC, 0666)) < 0) {
      km_err(2, "Cannot open corefile '%s', exiting", core_path);
   }
   km_warnx("Write %s to '%s'", dumptype == KM_DO_SNAP ? "snapshot" : "coredump", core_path);

   if (dumptype == KM_DO_SNAP && km_snapshot_ok() != 0) {
      km_errx(1, "Can't take a snapshot, active interval timer(s)");
   }

   if ((notes_buffer = (char*)calloc(1, notes_length)) == NULL) {
      km_err(2, "cannot allocate notes buffer, exiting");
   }
   offset = sizeof(Elf64_Ehdr) + phnum * sizeof(Elf64_Phdr);

   km_core_write_phdrs(vcpu, fd, phnum, end_load, notes_buffer, notes_length, label, description, &offset, dumptype);

   // Write the actual data.
   km_core_write(fd, notes_buffer, notes_length);
   km_infox(KM_TRACE_COREDUMP, "Dump executable");
   for (int i = 0; i < km_guest.km_ehdr.e_phnum; i++) {
      Elf64_Phdr* phdr = &km_guest.km_phdr[i];
      if (phdr->p_type != PT_LOAD) {
         continue;
      }
      size_t write_size = phdr->p_memsz;
      write_size += km_core_last_load_adjust(phdr, end_load);
      size_t extra = phdr->p_vaddr - rounddown(phdr->p_vaddr, KM_PAGE_SIZE);
      km_guestmem_write(fd,
                        rounddown(phdr->p_vaddr + km_guest.km_load_adjust, KM_PAGE_SIZE),
                        write_size + extra);
   }
   if (km_dynlinker.km_filename != NULL) {
      km_infox(KM_TRACE_COREDUMP, "Dump dynlinker");
      for (int i = 0; i < km_dynlinker.km_ehdr.e_phnum; i++) {
         Elf64_Phdr* phdr = &km_dynlinker.km_phdr[i];
         if (phdr->p_type != PT_LOAD) {
            continue;
         }
         size_t write_size = phdr->p_memsz;
         write_size += km_core_last_load_adjust(phdr, end_load);
         size_t extra = phdr->p_vaddr - rounddown(phdr->p_vaddr, KM_PAGE_SIZE);
         km_guestmem_write(fd,
                           rounddown(phdr->p_vaddr + km_dynlinker.km_load_adjust, KM_PAGE_SIZE),
                           write_size + extra);
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
            km_err(2, "failed to make %p,0x%lx readable for dump, exiting", start, ptr->size);
         }
      }
      km_guestmem_write(fd, ptr->start, ptr->size);
      // recover protection, in case it's a live coredump and we are not exiting yet
      if (ptr->km_flags.km_mmap_part_of_monitor == 0 && (ptr->protection & PROT_READ) != PROT_READ &&
          mprotect(start, ptr->size, ptr->protection) != 0) {
         km_err(2, "failed to set %p,0x%lx prot to 0x%x, exiting", start, ptr->size, ptr->protection);
      }
   }

   free(notes_buffer);
   (void)close(fd);
}
