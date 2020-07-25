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
 */

#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

char* cmdname;
char* socket_name = NULL;

void usage(void)
{
   fprintf(stderr, "usage: %s -s <usocket>\n", cmdname);
}
int main(int argc, char* argv[])
{
   int c;

   cmdname = argv[0];
   while ((c = getopt(argc, argv, "s:")) != -1) {
      switch (c) {
         case 's':
            socket_name = optarg;
            break;
         default:
            fprintf(stderr, "unrecognized option %c\n", c);
            usage();
            return 1;
      }
   }

   if (socket_name == NULL) {
      fprintf(stderr, "socket name required\n");
      return 1;
   }

   struct sockaddr_un addr = {.sun_family = AF_UNIX};
   if (strlen(socket_name) + 1 > sizeof(addr.sun_path)) {
      fprintf(stderr, "socket name too long\n");
      return 1;
   }
   strcpy(addr.sun_path, socket_name);

   int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
   if (sockfd < 0) {
      perror("socket");
      return 1;
   }
   if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      perror("connect");
      return 1;
   }

   close(sockfd);
   return 0;
}