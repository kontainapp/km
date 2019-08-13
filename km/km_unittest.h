#include "km.h"
#include "km_mem.h"

typedef enum {
   KM_UT_GET_MMAPS_INFO,
} km_unittest_op_t;

// allocated in payload, used in KM_UT_GET_MMAPS_INFO
typedef struct {
   int ntotal;             // [in] slots in buffer [out] total maps count on success
   int nfree;              // [out] fre maps count. (nbusy = ntotal -nfree )
   km_mmap_reg_t maps[];   // [out] maps. first free, then busy
} km_ut_get_mmaps_t;

extern int km_guest_unittest(km_vcpu_t* vcpu, km_unittest_op_t operation, km_kma_t param);
