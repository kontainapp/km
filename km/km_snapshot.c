/*
 * Copyright © 2020 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Kontain VM snapshot restart. Leverages KM extended ELF coredumps.
 *
 * TODO:
 *    - Open file descriptors (both FS and sockets)
 *    - Snapshot versioning
 */
#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/procfs.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "km.h"
#include "km_coredump.h"
#include "km_elf.h"
#include "km_filesys.h"
#include "km_gdb.h"
#include "km_guest.h"
#include "km_mem.h"
#include "km_signal.h"
#include "km_snapshot.h"

// TODO: Need to figure out where the snapshot default should go.
static char* snapshot_path = "./kmsnap";

static char* snapshot_input_path = NULL;
static char* snapshot_output_path = NULL;

void km_set_snapshot_path(char* path)
{
   km_infox(KM_TRACE_SNAPSHOT, "Setting snapshot path to %s", path);
   if ((snapshot_path = strdup(path)) == NULL) {
      km_err(1, "Failed to alloc memory for snapshot path");
   }
}

char* km_get_snapshot_path()
{
   return snapshot_path;
}

void km_set_snapshot_input_path(char* path)
{
   km_infox(KM_TRACE_SNAPSHOT, "Setting snapshot input to %s", path);
   if ((snapshot_input_path = strdup(path)) == NULL) {
      km_err(1, "Failed to alloc memory for snapshot input");
   }
}

char* km_get_snapshot_input_path()
{
   return snapshot_input_path;
}

void km_set_snapshot_output_path(char* path)
{
   km_infox(KM_TRACE_SNAPSHOT, "Setting snapshot output to %s", path);
   if ((snapshot_output_path = strdup(path)) == NULL) {
      km_err(1, "Failed to alloc memory for snapshot output");
   }
}

char* km_get_snapshot_output_path()
{
   return snapshot_output_path;
}

int km_snapshot_getdata(km_vcpu_t* vcpu, char* buf, int buflen)
{
   if (buf == NULL) {
      km_infox(KM_TRACE_SNAPSHOT, "bad buffer");
      return -EFAULT;
   }
   if (snapshot_input_path == NULL) {
      return 0;
   }
   int fd = km_internal_open(snapshot_input_path, O_RDONLY, 0);
   if (fd < 0) {
      km_info(KM_TRACE_SNAPSHOT, "open failure");
      return -errno;
   }
   int rc = read(fd, buf, buflen);
   if (rc < 0) {
      km_info(KM_TRACE_SNAPSHOT, "read failure");
      rc = -errno;
   }
   close(fd);
   return rc;
}

int km_snapshot_putdata(km_vcpu_t* vcpu, char* buf, int buflen)
{
   if (buf == NULL) {
      km_infox(KM_TRACE_SNAPSHOT, "bad buffer");
      return -EFAULT;
   }
   if (snapshot_output_path == NULL) {
      return 0;
   }
   int fd = km_internal_open(snapshot_output_path, O_RDWR | O_CREAT | O_TRUNC, 0666);
   if (fd < 0) {
      km_info(KM_TRACE_SNAPSHOT, "open failure");
      return -errno;
   }
   int rc = write(fd, buf, buflen);
   if (rc < 0) {
      km_info(KM_TRACE_SNAPSHOT, "write failure");
      rc = -errno;
   }
   close(fd);
   return rc;
}

/*
 * Recovers brk, tbrk and mingva from snapshot file.
 */
static inline void
km_ss_recover_memory_limits(km_payload_t* payload, km_gva_t* mingvap, km_gva_t* brkp, km_gva_t* tbrkp)
{
   /*
    * recover brk and tbrk
    */
   km_gva_t mingva = -1;
   km_gva_t rbrk = 0;
   km_gva_t rtbrk = -1;
   GElf_Ehdr* ehdr = &payload->km_ehdr;
   for (int i = 0; i < ehdr->e_phnum; i++) {
      GElf_Phdr* phdr = &payload->km_phdr[i];
      if (phdr->p_type == PT_LOAD) {
         km_infox(KM_TRACE_SNAPSHOT,
                  "%d PT_LOAD offset=0x%lx vaddr=0x%lx msize=0x%lx fsize=0x%lx flags=0x%x",
                  i,
                  phdr->p_offset,
                  phdr->p_vaddr,
                  phdr->p_memsz,
                  phdr->p_filesz,
                  phdr->p_flags);
         // Skip guest VDSO and KM unikernel
         if (km_vdso_gva(phdr->p_vaddr) != 0 || km_guestmem_gva(phdr->p_vaddr) != 0) {
            continue;
         }
         if (phdr->p_vaddr >= gpa_to_upper_gva(GUEST_MEM_START_VA)) {
            if (phdr->p_vaddr < rtbrk) {
               rtbrk = phdr->p_vaddr;
            }
         } else {
            if (phdr->p_vaddr < mingva) {
               mingva = phdr->p_vaddr;
            }
            if (phdr->p_vaddr + phdr->p_filesz > rbrk) {
               rbrk = phdr->p_vaddr + phdr->p_filesz;
            }
         }
      }
   }
   *mingvap = mingva;
   *brkp = rbrk;
   *tbrkp = rtbrk;
}

static inline void km_ss_recover_memory(int fd, km_payload_t* payload)
{
   GElf_Ehdr* ehdr = &payload->km_ehdr;

   /*
    * recover brk and tbrk
    */
   km_gva_t mingva = -1;
   km_gva_t rbrk = 0;
   km_gva_t rtbrk = -1;
   km_ss_recover_memory_limits(payload, &mingva, &rbrk, &rtbrk);

   /*
    * Allocate all of lower memory in one go.
    */
   if (km_mem_brk(rbrk) != rbrk) {
      km_err(2, "brk recover failure");
   }

   /*
    * machine_init maps VDSO and Guest runtime code into memory.
    * tbrk records how much mem this took. size high mmap
    * from tbrk.
    */
   km_gva_t tbrk_gva = km_mem_tbrk(0);
   km_gva_t ptr = km_guest_mmap(0, tbrk_gva - rtbrk, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   if (ptr != rtbrk) {
      km_errx(2, "tbrk recover failure: expect=0x%lx got=0x%lx", rtbrk, ptr);
   }
   for (int i = 0; i < ehdr->e_phnum; i++) {
      GElf_Phdr* phdr = &payload->km_phdr[i];
      if (phdr->p_type == PT_LOAD) {
         // Skip guest VDSO and KM unikernel
         if (km_vdso_gva(phdr->p_vaddr) != 0 || km_guestmem_gva(phdr->p_vaddr) != 0) {
            continue;
         }
         if (phdr->p_vaddr >= gpa_to_upper_gva(GUEST_MEM_START_VA)) {
            // upper: Skip addresses loaded by machine initialization.
            if (phdr->p_vaddr + phdr->p_filesz <= tbrk_gva) {
               int prot = prot_elf_to_mmap(phdr->p_flags);
               // Guest mprotect gets the KM mmap regs split.
               int ret = km_guest_mprotect(phdr->p_vaddr, phdr->p_filesz, prot);
               if (ret != 0) {
                  km_err(-ret, "km_guest_mprotect failed");
               }
               // mmap the data
               void* m = mmap(km_gva_to_kma(phdr->p_vaddr),
                              phdr->p_filesz,
                              prot,
                              MAP_PRIVATE | MAP_FIXED,
                              fd,
                              phdr->p_offset);
               if (m == MAP_FAILED) {
                  km_err(errno,
                         "snapshot mmap[%d]: vaddr=0x%lx offset=0x%lx \nexiting",
                         i,
                         phdr->p_vaddr,
                         phdr->p_offset);
               }
            }
         } else {
            // lower
            uint64_t extra = phdr->p_vaddr - rounddown((uint64_t)phdr->p_vaddr, KM_PAGE_SIZE);
            void* addr = km_gva_to_kma(phdr->p_vaddr - extra);
            void* m = mmap(addr,
                           phdr->p_filesz + extra,
                           prot_elf_to_mmap(phdr->p_flags),
                           MAP_PRIVATE | MAP_FIXED,
                           fd,
                           phdr->p_offset - extra);
            if (m == MAP_FAILED) {
               km_err(errno,
                      "snapshot mmap[%d]: vaddr=0x%lx offset=0x%lx \nexiting",
                      i,
                      phdr->p_vaddr,
                      phdr->p_offset);
            }
         }
      }
   }
}

/*
 * Read in notes. Allocates buffer for return. Caller responsible for
 * freeing buffer.
 */
static inline char* km_snapshot_read_notes(int fd, size_t* notesize, km_payload_t* payload)
{
   GElf_Ehdr* ehdr = &payload->km_ehdr;
   for (int i = 0; i < ehdr->e_phnum; i++) {
      GElf_Phdr* phdr = &payload->km_phdr[i];
      if (phdr->p_type == PT_NOTE) {
         char* notebuf = malloc(phdr->p_filesz);
         assert(notebuf != NULL);
         int rc;
         if ((rc = pread(fd, notebuf, phdr->p_filesz, phdr->p_offset)) != phdr->p_filesz) {
            if (rc < 0) {
               km_err(1, "read notes failed:");
            } else {
               km_errx(2, "read notes short: expect:%ld got:%d", phdr->p_filesz, rc);
            }
         }
         *notesize = phdr->p_filesz;
         return notebuf;
      }
   }
   return NULL;
}

/*
 * Restores NT_PRSTATUS information  for a guest thread (VCPU).
 */
static int km_ss_recover_prstatus(char* ptr, size_t length)
{
   if (length < sizeof(struct elf_prstatus)) {
      return -1;
   }
   struct elf_prstatus* pr = (struct elf_prstatus*)ptr;
   km_vcpu_t* vcpu = km_vcpu_restore(pr->pr_pid);
   if (vcpu == NULL) {
      km_errx(2, "failed to restore vcpu %d", pr->pr_pid - 1);
   }
   vcpu->regs.r15 = pr->pr_reg[0];
   vcpu->regs.r14 = pr->pr_reg[1];
   vcpu->regs.r13 = pr->pr_reg[2];
   vcpu->regs.r12 = pr->pr_reg[3];
   vcpu->regs.rbp = pr->pr_reg[4];
   vcpu->regs.rbx = pr->pr_reg[5];
   vcpu->regs.r11 = pr->pr_reg[6];
   vcpu->regs.r10 = pr->pr_reg[7];
   vcpu->regs.r9 = pr->pr_reg[8];
   vcpu->regs.r8 = pr->pr_reg[9];
   vcpu->regs.rax = pr->pr_reg[10];
   vcpu->regs.rcx = pr->pr_reg[11];
   vcpu->regs.rdx = pr->pr_reg[12];
   vcpu->regs.rsi = pr->pr_reg[13];
   vcpu->regs.rdi = pr->pr_reg[14];
   vcpu->regs.rax = pr->pr_reg[15];   // orig_ax?
   vcpu->regs.rip = pr->pr_reg[16];
   vcpu->sregs.cs.base = pr->pr_reg[17];
   vcpu->regs.rflags = pr->pr_reg[18];
   vcpu->regs.rsp = pr->pr_reg[19];
   vcpu->sregs.ss.base = pr->pr_reg[20];
   vcpu->sregs.fs.base = pr->pr_reg[21];
   vcpu->sregs.gs.base = pr->pr_reg[22];
   vcpu->sregs.ds.base = pr->pr_reg[23];
   vcpu->sregs.es.base = pr->pr_reg[24];
   vcpu->sregs.fs.base = pr->pr_reg[25];
   vcpu->sregs.gs.base = pr->pr_reg[26];
   vcpu->regs_valid = 1;
   km_write_registers(vcpu);
   vcpu->sregs_valid = 1;
   km_write_sregisters(vcpu);
   km_write_xcrs(vcpu);
   return 0;
}

/*
 * Restores KM specific information
 */
static int km_ss_recover_vcpu_info(char* ptr, size_t length)
{
   /*
    * This is a compile time check to remind developers to check
    * for snapshot implications when km_vcpu_t changes.
    */
   static_assert(sizeof(km_vcpu_t) == 960,
                 "sizeof(km_vcpu_t) changed. Check for snapshot implications");

   if (length < sizeof(km_nt_vcpu_t)) {
      return -1;
   }
   km_nt_vcpu_t* nt = (km_nt_vcpu_t*)ptr;
   km_vcpu_t* vcpu = machine.vm_vcpus[nt->vcpu_id];
   vcpu->stack_top = nt->stack_top;
   vcpu->guest_thr = nt->guest_thr;
   vcpu->set_child_tid = nt->set_child_tid;
   vcpu->clear_child_tid = nt->clear_child_tid;
   vcpu->sigaltstack.ss_sp = (void*)nt->sigaltstack_sp;
   vcpu->sigaltstack.ss_flags = nt->sigaltstack_flags;
   vcpu->sigaltstack.ss_size = nt->sigaltstack_size;
   vcpu->mapself_base = nt->mapself_base;
   vcpu->mapself_size = nt->mapself_size;
   km_hcargs[HC_ARGS_INDEX(nt->vcpu_id)] = (km_hc_args_t*)nt->hcarg;
   vcpu->hypercall = nt->hypercall;
   vcpu->restart = nt->restart;

   // Restore Floating point unit/vector for this VCPU
   if (km_vmdriver_restore_fpstate(vcpu, nt + 1, nt->fp_format) < 0) {
      km_warnx("Error restoring FP state");
   }
   return 0;
}

/*
 * Recover machine.mmaps from NT_FILE records.
 *
 * Record format for NT_FILE:
 *   uint64_t nfile;   // Number of file map records
 *   uint64_t pagesz;  // Page size (We dont use)
 *   // Followed by nfile records (packed) that look like this
 *   uint64_t base;    // base virtual address of the region
 *   uint64_t limit;   // Address of end of region
 *   uint64_t pagenum; // Page Number (we dont' use)
 *   // Followed by nfile NULL terminated strings
 */
static int km_ss_recover_file_maps(char* ptr, size_t length)
{
   uint64_t nfile = ((uint64_t*)ptr)[0];

   // start of region descriptions
   uint64_t* current = &((uint64_t*)ptr)[2];
   // start of region filename strings
   char* curnm = (char*)&((uint64_t*)ptr)[2 + 3 * nfile];
   for (uint64_t i = 0; i < nfile; i++) {
      km_gva_t base = current[0];
      km_gva_t limit = current[1];
      if (limit >= gpa_to_upper_gva(GUEST_MEM_START_VA)) {
         /*
          * There are NT_FILE records for all mappings, including the one
          * in low virtual memory create by load_elf. We want to skip those
          * here and only recover the areas created by guest mmap calls.
          * km_guest/km_dynlinker recovery is done with NT_KM_GUEST and
          * NT_KM_DYNLINKER records. See km_ss_recover_payload.
          */
         km_mmap_set_filename(base, limit, curnm);
      }
      current = &current[3];
      curnm += strlen(curnm) + 1;
   }
   return 0;
}

/*
 * Recover payload state.
 */
static inline int km_ss_recover_payload(char* ptr, size_t length, km_payload_t* payload)
{
   char* cur = ptr;
   km_nt_guest_t* g = (km_nt_guest_t*)cur;

   payload->km_load_adjust = g->load_adjust;
   payload->km_ehdr = g->ehdr;
   cur += sizeof(km_nt_guest_t);

   Elf64_Phdr* phdr = malloc(payload->km_ehdr.e_phnum * sizeof(Elf64_Phdr));
   memcpy(phdr, cur, payload->km_ehdr.e_phnum * sizeof(Elf64_Phdr));
   payload->km_phdr = phdr;
   cur += payload->km_ehdr.e_phnum * sizeof(Elf64_Phdr);

   payload->km_filename = strdup(cur);
   return 0;
}

static inline int km_ss_recover_guest(char* ptr, size_t length)
{
   return km_ss_recover_payload(ptr, length, &km_guest);
}

static inline int km_ss_recover_dynlinker(char* ptr, size_t length)
{
   return km_ss_recover_payload(ptr, length, &km_dynlinker);
}

int km_snapshot_notes_apply(char* notebuf, size_t notesize, int type, int (*func)(char*, size_t))
{
   char* cur = notebuf;
   while (cur - notebuf < notesize) {
      Elf64_Nhdr* nhdr = (Elf64_Nhdr*)cur;
      if (nhdr->n_type == type) {
         int ret = func(cur + sizeof(Elf64_Nhdr) + nhdr->n_namesz, nhdr->n_descsz);
         if (ret < 0) {
            return ret;
         }
      }

      cur += roundup(sizeof(Elf64_Nhdr) + nhdr->n_namesz + nhdr->n_descsz, sizeof(Elf64_Word));
   }
   return 0;
}

static inline int km_ss_recover_vcpus(char* notebuf, size_t notesize)
{
   int ret;
   if ((ret = km_snapshot_notes_apply(notebuf, notesize, NT_PRSTATUS, km_ss_recover_prstatus)) < 0) {
      return ret;
   }
   if ((ret = km_snapshot_notes_apply(notebuf, notesize, NT_KM_VCPU, km_ss_recover_vcpu_info)) < 0) {
      return ret;
   }
   return 0;
}

static inline int km_ss_recover_km_monitor(char* notebuf, size_t notesize)
{
   km_nt_monitor_t* mon = (km_nt_monitor_t*)notebuf;
   char* base = (char*)(mon + 1);
   if (mon->label_length > 0) {
      km_warnx("label: %s", base);
   }
   if (mon->description_length > 0) {
      km_warnx("description: %s", base + mon->label_length);
   }
   // Verify that the virtualization driver used when the snapshot was taken is the same
   // as the one km was told to use with the -F (or --virt-device) command flag.
   if (!(mon->monitor_type == KM_NT_MONITOR_TYPE_KVM && machine.vm_type == VM_TYPE_KVM) &&
       !(mon->monitor_type == KM_NT_MONITOR_TYPE_KKM && machine.vm_type == VM_TYPE_KKM)) {
      char* snapshot_vmtype[] = {"kvm", "kkm"};   // indexed by monitor_type
      char* current_vmtype[] = {"kvm", "kkm"};    // indexed by vm_type_t
      km_warnx("snapshot virtualization (%s) and current virtualization (%s) do not agree",
               snapshot_vmtype[mon->monitor_type],
               current_vmtype[machine.vm_type]);
      return -1;
   }
   return 0;
}

int km_snapshot_restore(km_elf_t* e)
{
   km_payload_t tmp_payload = {};

   tmp_payload.km_ehdr = e->ehdr;

   // Read ELF PHDR into tmp_payload.
   if ((tmp_payload.km_phdr = alloca(sizeof(Elf64_Phdr) * e->ehdr.e_phnum)) == NULL) {
      km_err(2, "no memory for elf program headers");
   }
   for (int i = 0; i < tmp_payload.km_ehdr.e_phnum; i++) {
      GElf_Phdr* phdr = &tmp_payload.km_phdr[i];

      if (gelf_getphdr(e->elf, i, phdr) == NULL) {
         km_errx(2, "gelf_getphrd %i, %s", i, elf_errmsg(-1));
      }
   }

   // disable mmap consolidation during recovery
   km_mmap_set_recovery_mode(1);

   // Memory is fully described by PT_LOAD sections
   km_ss_recover_memory(e->fd, &tmp_payload);

   // VCPU's are described in the PT_NOTES section
   size_t notesize = 0;
   char* notebuf = km_snapshot_read_notes(e->fd, &notesize, &tmp_payload);
   if (notebuf == NULL) {
      km_errx(2, "PT_NOTES not found");
   }
   km_close_elf_file(e);   // close now to avoid collision with fd's that need restoring

   if (km_snapshot_notes_apply(notebuf, notesize, NT_KM_MONITOR, km_ss_recover_km_monitor) < 0) {
      km_errx(2, "recover monitor failed");
   }
   if (km_ss_recover_vcpus(notebuf, notesize) < 0) {
      km_errx(2, "VCPU restore failed");
   }

   if (km_snapshot_notes_apply(notebuf, notesize, NT_FILE, km_ss_recover_file_maps) < 0) {
      km_errx(2, "recover file maps failed");
   }
   if (km_snapshot_notes_apply(notebuf, notesize, NT_KM_GUEST, km_ss_recover_guest) < 0) {
      km_errx(2, "recover guest payload failed");
   }
   if (km_snapshot_notes_apply(notebuf, notesize, NT_KM_DYNLINKER, km_ss_recover_dynlinker) < 0) {
      km_errx(2, "recover dynlinker payload maps failed");
   }
   if (km_snapshot_notes_apply(notebuf, notesize, NT_KM_SIGHAND, km_sig_snapshot_recover) < 0) {
      km_errx(2, "recover signal handlers failed");
   }
   if (km_fs_recover(notebuf, notesize) < 0) {
      km_errx(2, "recover open files failed");
   }

   /*
    * km_vcpu_get() assumes that VCPU tasks are allocated and never freed.
    * Create any needed idle vcpu threads needed to meet this assumption.
    */
   int top_vcpu = -1;
   for (int i = 0; i < KVM_MAX_VCPUS; i++) {
      if (machine.vm_vcpus[i] != NULL) {
         top_vcpu = i;
      }
   }
   for (int i = 0; i < top_vcpu; i++) {
      if (machine.vm_vcpus[i] == NULL) {
         machine.vm_vcpus[i] = km_vcpu_restore(i + 1);
         km_vcpu_put(machine.vm_vcpus[i]);
      }
   }

   // reenable mmap consolidation
   km_mmap_set_recovery_mode(0);
   free(notebuf);
   return 0;
}

int km_snapshot_create(km_vcpu_t* vcpu, char* label, char* description, int live)
{
   // No snapshots while GDB is running
   if (km_gdb_is_enabled() != 0) {
      km_warnx("Cannot create snapshot with GDB running");
      return -EBUSY;
   }

   static pthread_mutex_t snap_mutex = PTHREAD_MUTEX_INITIALIZER;
   static int in_snapshot = 0;
   pthread_mutex_lock(&snap_mutex);
   if (in_snapshot != 0) {
      pthread_mutex_unlock(&snap_mutex);
      km_warnx("Only one snapshot request allowed at a time");
      return -EBUSY;
   }
   in_snapshot = 1;
   pthread_mutex_unlock(&snap_mutex);

   km_vcpu_pause_all(vcpu, ALL);   // Wait for everyone to get to the pause point.

   /*
    * TODO: Work label and description into this.
    */
   km_dump_core(km_get_snapshot_path(), vcpu, NULL, label, description, KM_DO_SNAP);

   if (live == 0) {
      machine.exit_group = 1;
   }
   pthread_mutex_lock(&snap_mutex);
   in_snapshot = 0;
   pthread_mutex_unlock(&snap_mutex);

   km_vcpu_resume_all();
   return 0;
}
