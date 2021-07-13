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

#ifndef __KM_KKM_H__
#define __KM_KKM_H__

#include "km_mem.h"

#define KKM_SAVE_INFO_SZ (64)
/*
 * KKM_KONTEXT_GET_SAVE_INFO and KKM_KONTEXT_SET_SAVE_INFO
 * opaque data to save and restore information related to KKM
 */
typedef struct kkm_save_info {
   uint8_t data[KKM_SAVE_INFO_SZ];
} kkm_save_info_t;
static_assert(sizeof(struct kkm_save_info) == 64,
              "kkm_save_info is known to monitor, size is fixed at 64 bytes");

/*
 * KKM_KONTEXT_GET_XSTATE and KKM_KONTEXT_SET_XSTATE
 */
typedef struct kkm_xstate {
   uint8_t data[KM_PAGE_SIZE];
} kkm_xstate_t;
static_assert(sizeof(struct kkm_xstate) == 4096, "kkm_xstate need to be in sync with kkm module");

/*
 * additional ioctls supported by KKM
 */
#define KKM_KONTEXT_REUSE _IO(KVMIO, 0xf5)
#define KKM_KONTEXT_GET_SAVE_INFO _IOR(KVMIO, 0xf6, struct kkm_save_info)
#define KKM_KONTEXT_SET_SAVE_INFO _IOW(KVMIO, 0xf7, struct kkm_save_info)
#define KKM_KONTEXT_GET_XSTATE _IOR(KVMIO, 0xf8, struct kkm_xstate)
#define KKM_KONTEXT_SET_XSTATE _IOW(KVMIO, 0xf9, struct kkm_xstate)

#define KKM_GET_IDENTITY _IO(KVMIO, 0xff)
#define KKM_DEVICE_IDENTITY (0x6B6B6D)

int km_kkm_vcpu_init(km_vcpu_t* vcpu);
int km_kkm_set_save_info(km_vcpu_t* vcpu, uint8_t ksi_valid, kkm_save_info_t* ksi);
int km_kkm_get_save_info(km_vcpu_t* vcpu, kkm_save_info_t* ksi);
int km_kkm_set_xstate(km_vcpu_t* vcpu, uint8_t kx_valid, kkm_xstate_t* kx);
int km_kkm_get_xstate(km_vcpu_t* vcpu, kkm_xstate_t* kx);

#endif /* #ifndef __KM_KKM_H__ */
