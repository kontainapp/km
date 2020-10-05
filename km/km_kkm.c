/*
 * Copyright © 2020-2020 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */

/*
 * Deviations from standard KVM ioctl behavior tracked in this file
 */

#include <assert.h>
#include <linux/kvm.h>

#include "km.h"
#include "km_kkm.h"

int km_kkm_vcpu_init(km_vcpu_t* vcpu)
{
   int retval = 0;

   if ((retval = ioctl(vcpu->kvm_vcpu_fd, KKM_KONTEXT_REUSE)) == -1) {
      km_warn("VCPU reinit failed vcpu id %d vcpu fd %d", vcpu->vcpu_id, vcpu->kvm_vcpu_fd);
   }
   return retval;
}

int km_kkm_set_save_info(km_vcpu_t* vcpu, uint8_t ksi_valid, kkm_save_info_t* ksi)
{
   int retval = 0;

   if (ksi_valid == 0) {
      return 0;
   }

   if ((retval = ioctl(vcpu->kvm_vcpu_fd, KKM_KONTEXT_SET_SAVE_INFO, ksi)) == -1) {
      km_warn("VCPU set save info failed vcpu id %d vcpu fd %d", vcpu->vcpu_id, vcpu->kvm_vcpu_fd);
   }
   return retval;
}

int km_kkm_get_save_info(km_vcpu_t* vcpu, kkm_save_info_t* ksi)
{
   int retval = 0;

   if ((retval = ioctl(vcpu->kvm_vcpu_fd, KKM_KONTEXT_GET_SAVE_INFO, ksi)) == -1) {
      km_warn("VCPU get save info failed vcpu id %d vcpu fd %d", vcpu->vcpu_id, vcpu->kvm_vcpu_fd);
   }
   return retval;
}

int km_kkm_set_xstate(km_vcpu_t* vcpu, uint8_t kx_valid, kkm_xstate_t* kx)
{
   int retval = 0;

   if (kx_valid == 0) {
      return 0;
   }

   if ((retval = ioctl(vcpu->kvm_vcpu_fd, KKM_KONTEXT_SET_XSTATE, kx)) == -1) {
      km_warn("VCPU set xstate failed vcpu id %d vcpu fd %d", vcpu->vcpu_id, vcpu->kvm_vcpu_fd);
   }
   return retval;
}

int km_kkm_get_xstate(km_vcpu_t* vcpu, kkm_xstate_t* kx)
{
   int retval = 0;

   if ((retval = ioctl(vcpu->kvm_vcpu_fd, KKM_KONTEXT_GET_XSTATE, kx)) == -1) {
      km_warn("VCPU get xstate failed vcpu id %d vcpu fd %d", vcpu->vcpu_id, vcpu->kvm_vcpu_fd);
   }
   return retval;
}
