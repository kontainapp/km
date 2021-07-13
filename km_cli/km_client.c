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