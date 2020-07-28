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
 *
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

static int sock = -1;
static struct sockaddr_un addr = {.sun_family = AF_UNIX};
pthread_t thread;
int kill_thread = 0;

static void* mgt_main(void* arg)
{
   if (listen(sock, 1) < 0) {
      km_err_msg(errno, "listen");
      return NULL;
   }

   /*
    * First implementation is really dumb. Listen on a socket. When a connect
    * happens, snapshot the guest. This has the advantage of not doing anything
    * about message formats and the like for now.
    */
   while (kill_thread == 0) {
      int nfd = km_internal_accept(sock, NULL, NULL);
      warnx("Connection accepted");
      close(nfd);

      km_vcpu_pause_all();
      km_dump_core(km_get_snapshot_path(), NULL, NULL);
      unlink(addr.sun_path);
      exit(0);
   }
   return NULL;
}

void km_mgt_init(char* path)
{
   if (path == NULL) {
      return;
   }
   if (strlen(path) + 1 > sizeof(addr.sun_path)) {
      errx(2, "mgmt path too long");
   }
   strcpy(addr.sun_path, path);

   if ((sock = km_internal_socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
      err(2, "mgt socket(2)");
   }
   if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      km_err_msg(errno, "bind failure: %s", path);
      goto err;
   }
   if (pthread_create(&thread, NULL, mgt_main, NULL) != 0) {
      km_err_msg(errno, "phtread_create failure");
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
   close(sock);
   return;
}