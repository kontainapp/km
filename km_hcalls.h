/*
 * TODO: Header
 */

#include <stdint.h>

/*
 * Definitions of hypercalls guest code (payload) can make into the KontainVM.
 * This file is to be included in both KM code and the guest library code.
 */
static const int KM_HCALL_PORT_BASE = 0x8000;

typedef enum km_hcall {
   KM_HC_BASE = 0,
   KM_HC_HLT = 0,
   KM_HC_STDOUT,
   KM_HC_COUNT
} km_hcall_t;

typedef struct km_hlt_hc {
   int exit_code;
} km_hlt_hc_t;

typedef struct km_stdout_hc {
   uint64_t data;
   uint32_t length;
} km_stdout_hc_t;
