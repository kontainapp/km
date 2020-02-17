/*
 * Copyright © 2020 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication of
 * such source code. Disclosure of this source code or any related proprietary
 * information is strictly prohibited without the express written permission of
 * Kontain Inc.
 */

/*
 * Various functions for getting information from /proc.
 * And it may be used for handling the payload's synthesized /proc filesystem.
 */

#include <stdio.h>
#include <errno.h>
#include <sched.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#include "km.h"
#include "km_proc.h"

#define PROC_SELF_MAPS "/proc/self/maps"

/*
 * Read /proc/self/maps looking for the entries that match the names supplied.
 */
int km_find_maps_regions(maps_region_t* regions, uint32_t nregions)
{
   FILE* procmaps;
   char linebuffer[256];
   char prot[strlen("rwxp") + 4];
   int rv = -1;
   int foundcount = 0;

   for (int i = 0; i < nregions; i++) regions[i].found = 0;

   procmaps = fopen(PROC_SELF_MAPS, "r");
   if (procmaps == NULL) {
      km_info(KM_TRACE_MEM, "can't open %s for reading", PROC_SELF_MAPS);
      return -2;
   }

   while (fgets(linebuffer, sizeof(linebuffer), procmaps) != NULL) {
      int i;
      for (i = 0; i < nregions; i++) {
         if (regions[i].found == 0 && strstr(linebuffer, regions[i].name_substring) != NULL) {
            break;
         }
      }
      if (i < nregions) {  // We want this one, harvest the information
         if (sscanf(linebuffer, "%lx-%lx %5s ", &regions[i].begin_addr, &regions[i].end_addr, prot) == 3) {
            regions[i].allowed_access = (prot[0] == 'r' ? PROT_READ : 0) |
                                        (prot[1] == 'w' ? PROT_WRITE : 0) |
                                        (prot[2] == 'x' ? PROT_EXEC : 0);
            regions[i].found = 1;
            foundcount++;
         } else {
            km_infox(KM_TRACE_MEM, "Ignoring mangled line: %s in %s", linebuffer, PROC_SELF_MAPS);
         }
      }
      if (foundcount >= nregions) {
         break;
      }
   }

   fclose(procmaps);
   return rv;
}
