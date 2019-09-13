/*
 * Copyright © 2019 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Test helper to print out misc. info about memory slots in different memory sizes
 *
 * Print date in using four different locales
 */
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void print_time(void)
{
   static char outstr[4096];
   time_t t;
   struct tm* tmp;

   t = time(NULL);
   tmp = localtime(&t);
   if (tmp == NULL) {
      perror("localtime");
      exit(1);
   }

   if (strftime(outstr, sizeof(outstr), "%A, %d %B %Y %T %P %z %Z", tmp) < 0) {
      perror("strftime");
      exit(1);
   }
   printf("%s: ", outstr);
}

int main(int argc, char** argv)
{
   locale_t loc;

   if ((loc = uselocale((locale_t)0)) == NULL) {
      perror("uselocale(0)");
      exit(1);
   }
   print_time();
   printf("after 0: %p\n", loc);
   if ((loc = uselocale(LC_GLOBAL_LOCALE)) == NULL) {
      perror("uselocale(LC_GLOBAL_LOCALE)");
      exit(1);
   }
   print_time();
   printf("after LC_GLOBAL_LOCALE: %p\n", loc);
   if ((loc = newlocale(LC_ALL, "ru_RU.utf8", (locale_t)0)) == (locale_t)0) {
      perror("newlocale()");
      exit(1);
   }
   if (uselocale(loc) == NULL) {
      perror("uselocale(loc)");
      exit(1);
   }
   print_time();
   printf("after loc: %p\n", loc);
   if ((loc = newlocale(LC_ALL, "de_BE.utf8", (locale_t)0)) == (locale_t)0) {
      perror("newlocale()");
      exit(1);
   }
   if (uselocale(loc) == NULL) {
      perror("uselocale(loc)");
      exit(1);
   }
   print_time();
   printf("after loc: %p\n", loc);
}
