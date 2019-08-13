#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../km/km_unittest.h"
#include "km_hcalls.h"
#include "syscall.h"

static const int MAX_MAPS = 4096;

static int get_maps(void)
{
   void* param = calloc(1, sizeof(km_ut_get_mmaps_t) + MAX_MAPS * sizeof(km_mmap_reg_t));

   printf("calling unittest get map info ... %d\n",
          syscall(HC_km_unittest, KM_UT_GET_MMAPS_INFO, param));
   km_ut_get_mmaps_t* info = (km_ut_get_mmaps_t*)param;
   printf("free %d busy %d\n", info->nfree, info->nbusy);
   free(param);

   return 0;
}

int main(int argc, char* argv[])
{
   return get_maps();
}