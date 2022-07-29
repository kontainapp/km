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

/*
 * KM Management/Control Plane.
 *
 * There is a management thread responsible for listening on a UNIX domain socket
 * for management requests.
 */

#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "km_coredump.h"
#include "km_filesys.h"
#include "km_management.h"
#include "km_snapshot.h"
#include "libkontain_mgmt.h"

static int sock = -1;
static struct sockaddr_un addr = {.sun_family = AF_UNIX};
pthread_t thread;
int kill_thread = 0;

static void* mgt_main(void* arg)
{
   ssize_t br;
   mgmtrequest_t mgmtrequest;
   mgmtreply_t mgmtreply;
   int wewanttodie;

   if (listen(sock, 1) < 0) {
      km_warn("listen");
      return NULL;
   }

   /*
    * First implementation is really dumb. Listen on a socket. When a connect
    * happens, snapshot the guest. This has the advantage of not doing anything
    * about message formats and the like for now.
    */
   while (kill_thread == 0) {
      wewanttodie = 0;
      int nfd = km_mgt_accept(sock, NULL, NULL);
      km_tracex("Connection accepted");
      if (nfd < 0) {
         // Probably fini closed the listening socket, wait for kill_thread to drop us out of the loop.
         continue;
      }

      // Read the request.
      br = recv(nfd, &mgmtrequest, sizeof(mgmtrequest), 0);
      if (br < 0) {
         km_warn("recv mgmt request failed");
         close(nfd);
         continue;
      }
      if (br < 2 * sizeof(int)) {
         km_warnx("mgmt request is too short, %ld bytes", br);
         close(nfd);
         continue;
      }

      switch (mgmtrequest.opcode) {
         case KM_MGMT_REQ_SNAPSHOT:
            mgmtreply.request_status =
                km_snapshot_create(NULL,
                                   mgmtrequest.requests.snapshot_req.label,
                                   mgmtrequest.requests.snapshot_req.description,
                                   mgmtrequest.requests.snapshot_req.snapshot_path,
                                   mgmtrequest.requests.snapshot_req.live);
            wewanttodie = (mgmtrequest.requests.snapshot_req.live == 0);
            break;
         default:
            km_warnx("Unknown mgmt request %d, length %d", mgmtrequest.opcode, mgmtrequest.length);
            mgmtreply.request_status = EINVAL;
            break;
      }

      // let them know what happened.
      ssize_t bw = send(nfd, &mgmtreply, sizeof(mgmtreply), MSG_NOSIGNAL);
      if (bw != sizeof(mgmtreply)) {
         km_warn("send mgmt reply failed, byteswritten %ld", bw);
      }
      close(nfd);

      if (wewanttodie != 0) {
         exit(0);
      }
   }
   return NULL;
}

void km_mgt_init(char* path)
{
   if (path == NULL) {
      return;
   }
   if (strlen(path) + 1 > sizeof(addr.sun_path)) {
      km_errx(2, "mgmt path too long");
   }
   strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

   if ((sock = km_mgt_listen(AF_UNIX, SOCK_STREAM, 0)) < 0) {
      km_err(2, "mgt socket(2)");
   }
   if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      km_warn("bind failure: %s", path);
      goto err;
   }
   if (pthread_create(&thread, NULL, mgt_main, NULL) != 0) {
      km_warn("phtread_create failure");
      goto err;
   }
   return;
err:
   close(sock);
   sock = -1;
   return;
}

void km_mgt_fini(void)
{
   if (sock == -1) {
      return;
   }
   unlink(addr.sun_path);
   kill_thread = 1;
   close(sock);
   return;
}
