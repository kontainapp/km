/*
 * Copyright © 2018 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Invoke argv[1] hypercall. Used to test unsupported hypercalls and
 * bad hypercall argument pointer.
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "km_hcalls.h"

char *cmdname = "hc_call";
int badarg = 0;

void usage() {
   fprintf(stderr, "usage %s [options] <callid>\n", cmdname);
   fprintf(stderr, "  --bad-arg,-b   Pass a bad argument to hypercall\n");
}

int main(int argc, char** argv)
{
   extern int optind;
   int c;
   char *ep = NULL;
   int callid;
   static struct option long_options[] = {
      {"bad-arg", no_argument, 0, 'b'},
      {0, 0, 0, 0}
   };

   cmdname = argv[0];
   while ((c = getopt_long(argc, argv, "bh", long_options, NULL)) != -1) {
      switch (c) {
      case 'b':
         badarg = 1;
         break;
      case 'h':
         usage();
         return 0;
      default:
         fprintf(stderr, "unrecognized option %c\n", c);
         usage();
         return 1;
      }
   }

   if (argc != optind + 1) {
      exit(1);
   }
   callid = strtol(argv[optind], &ep, 0);
   if (ep == NULL || *ep != '\0') {
      fprintf(stderr, "callid '%s' is not a number", argv[optind]);
      usage();
      return 1;
   }

   if (badarg) {
      km_hcall(callid, (km_hc_args_t *) -1LL);
   } else {
      syscall(callid, 0);
   }

   exit(0);
}
