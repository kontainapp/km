#include "km_unittest.h"

#include <stdlib.h>

#ifdef _KM_UNITTEST
// return 0 (success) or -errno (ops-specific)
int km_guest_unittest(km_vcpu_t* vcpu, km_unittest_op_t operation, km_kma_t param)
{
   switch (operation) {
      case KM_UT_GET_MMAPS_INFO:
         // return info->ntotal for actual count. Return 0 and fill in info->nfree an info->maps if
         // enough room . Otherwise return -EAGAIN
         // Scans mmaps brute force. NOT thread safe.
         {
            km_ut_get_mmaps_t* info = (km_ut_get_mmaps_t*)param;
            km_mmap_reg_t *reg = NULL, *map = info->maps;
            int nfree = 0, nbusy = 0;
            // km_mmaps_lock();
            TAILQ_FOREACH (reg, &machine.mmaps.free, link) {
               if (nfree >= info->ntotal) {
                  return -EAGAIN;
               }
               *map++ = *reg;
               nfree++;
            }
            TAILQ_FOREACH (reg, &machine.mmaps.busy, link) {
               if (nfree + nbusy >= info->ntotal) {
                  return -EAGAIN;
               }
               *map++ = *reg;
               nbusy++;
            }
            info->ntotal = nfree + nbusy;
            info->nfree = nfree;
            // km_mmaps_unlock();
         }
         break;

      default:
         assert("Not reachable" == NULL);
   }
   return 0;
}

#endif
