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
 * This is an abstraction layer between KM and VM driver specifics. It is only a beginning and isn't
 * complete.
 */

#include "km.h"
#include "km_coredump.h"
#include "km_kkm.h"

void km_vmdriver_machine_init()
{
   switch (machine.vm_type) {
      case VM_TYPE_KVM:
         // does kvm support xsave?
         if (ioctl(machine.mach_fd, KVM_CHECK_EXTENSION, KVM_CAP_XSAVE) == 1) {
            machine.vmtype_u.kvm.xsave = 1;
         }
         break;
      case VM_TYPE_KKM:
         // Anything for KKM?
         break;
   }
}

/*
 * KKM driver keeps debug and syscall state.
 * When KM reuses VCPU this state becomes stale.
 * This ioctl bring KKM and KM state to sync.
 */
void km_vmdriver_vcpu_init(km_vcpu_t* vcpu)
{
   if (machine.vm_type == VM_TYPE_KKM) {
      km_kkm_vcpu_init(vcpu);
   }
}

static const int KVM_OLDEST_KERNEL = 0x040f;   // Ubuntu 16.04 with 4.15 kernel
static const int KKM_OLDEST_KERNEL = 0x0500;   // KKM only tested on 5+ for now

int km_vmdriver_lowest_kernel()
{
   return (machine.vm_type == VM_TYPE_KVM) ? KVM_OLDEST_KERNEL : KKM_OLDEST_KERNEL;
}

/*
 * KKM specific signal frame.
 */
typedef struct km_signal_kkm_frame {
   uint8_t ksi_valid;
   uint8_t kx_valid;
   kkm_save_info_t ksi;
   kkm_xstate_t kx;
} km_signal_kkm_frame_t;

size_t km_vmdriver_fpstate_size()
{
   size_t fstate_size = 0;
   switch (machine.vm_type) {
      case VM_TYPE_KVM:
         fstate_size =
             (machine.vmtype_u.kvm.xsave == 0) ? sizeof(struct kvm_fpu) : sizeof(struct kvm_xsave);
         break;
      case VM_TYPE_KKM:
         fstate_size = sizeof(km_signal_kkm_frame_t);
         break;
   }
   return fstate_size;
}

void km_vmdriver_save_fpstate(km_vcpu_t* vcpu, void* addr)
{
   switch (machine.vm_type) {
      case VM_TYPE_KKM: {
         km_signal_kkm_frame_t* kkm_frame = (km_signal_kkm_frame_t*)(addr);
         kkm_frame->ksi_valid = (km_kkm_get_save_info(vcpu, &kkm_frame->ksi) == 0) ? 1 : 0;
         kkm_frame->kx_valid = (km_kkm_get_xstate(vcpu, &kkm_frame->kx) == 0) ? 1 : 0;
         break;
      }
      case VM_TYPE_KVM: {
         void* kvm_frame = addr;
         if (machine.vmtype_u.kvm.xsave == 0) {
            if (ioctl(vcpu->kvm_vcpu_fd, KVM_GET_FPU, kvm_frame) < 0) {
               km_warn("KVM_GET_FPU failed");
            }
         } else {
            if (ioctl(vcpu->kvm_vcpu_fd, KVM_GET_XSAVE, kvm_frame) < 0) {
               km_warn("KVM_GET_XSAVE failed");
            }
         }
         break;
      }
      default:
         km_warnx("Unrecognized VM monitor type: %d", machine.vm_type);
         break;
   }
}

void km_vmdriver_restore_fpstate(km_vcpu_t* vcpu, void* addr)
{
   switch (machine.vm_type) {
      case VM_TYPE_KKM: {
         km_signal_kkm_frame_t* kkm_frame = (km_signal_kkm_frame_t*)addr;
         km_kkm_set_save_info(vcpu, kkm_frame->ksi_valid, &kkm_frame->ksi);
         km_kkm_set_xstate(vcpu, kkm_frame->kx_valid, &kkm_frame->kx);
         break;
      }
      case VM_TYPE_KVM: {
         void* kvm_frame = addr;
         if (machine.vmtype_u.kvm.xsave == 0) {
            if (ioctl(vcpu->kvm_vcpu_fd, KVM_SET_FPU, kvm_frame) < 0) {
               km_warn("KVM_SET_FPU failed");
            }
         } else {
            if (ioctl(vcpu->kvm_vcpu_fd, KVM_SET_XSAVE, kvm_frame) < 0) {
               km_warn("KVM_SET_XSAVE failed");
            }
         }
         break;
      }
      default:
         km_warnx("Unrecognized VM monitor type: %d", machine.vm_type);
         break;
   }
}
/*
 * KKM clone child handling changes depending on
 * child is created using hypercall or syscall.
 * Parent save this information in cpu_run area next to io data.
 * Copy this from parent to child
 */
void km_vmdriver_clone(km_vcpu_t* vcpu, km_vcpu_t* new_vcpu)
{
   kvm_run_t* parent_r = vcpu->cpu_run;
   uint32_t offset = parent_r->io.data_offset;

   if (machine.vm_type == VM_TYPE_KKM) {
      // copy KKM blob from parent to child currently 8 bytes
      *(uint64_t*)((km_kma_t)new_vcpu->cpu_run + offset) =
          *(uint64_t*)((km_kma_t)vcpu->cpu_run + offset);
      new_vcpu->regs.rax = 0;
   }
}

/*
 * KKM internal state saved and restored between old and new vcpus
 * currently used in fork
 */
void km_vmdriver_save_fork_info(km_vcpu_t* vcpu, uint8_t* ksi_valid, void* ksi)
{
   if (machine.vm_type == VM_TYPE_KKM) {
      *ksi_valid = (km_kkm_get_save_info(vcpu, ksi) == 0) ? 1 : 0;
      // km_kkm_get_save_info is destructive. restore it now before returning to caller.
      km_kkm_set_save_info(vcpu, *ksi_valid, ksi);
   }
}

void km_vmdriver_restore_fork_info(km_vcpu_t* vcpu, uint8_t ksi_valid, void* ksi)
{
   if (machine.vm_type == VM_TYPE_KKM) {
      km_kkm_set_save_info(vcpu, ksi_valid, ksi);
   }
}

int km_vmdriver_fp_format(km_vcpu_t* vcpu)
{
   switch (machine.vm_type) {
      case VM_TYPE_KVM:
         if (machine.vmtype_u.kvm.xsave == 0) {
            return NT_KM_VCPU_FPDATA_KVM_FPU;
         }
         return NT_KM_VCPU_FPDATA_KVM_XSAVE;
      case VM_TYPE_KKM:
         return NT_KM_VCPU_FPDATA_KKM_XSAVE;
   }
   return NT_KM_VCPU_FPDATA_NONE;
}
