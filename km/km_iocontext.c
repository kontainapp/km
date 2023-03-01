/*
 * Copyright 2023 Kontain Inc
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
 * To be able to use asynchronous i/o with snapshots km keeps its own asynch io context
 * id space and passes thoses id's to the payload.  Payload asynch i/o calls use the
 * km io context id's to the io_*() hypercalls.  When an io_setup() call is made into km,
 * km will call lilnux's io_setup() to get a context id from the kernel, and then put the
 * kernel and km context id's into an entry that maps between km /payload context id's and
 * kernel context id's.  And will then pass the km io context id to the payload.
 * Why is this done?
 * Basically to allow resumed km payload snapshots to work with asynch i/o.
 * The snapshoted payload's io context id's are created in the non-snapshot run of the payload
 * but must work in the resumed snapshot so we can't let the payload have the kernel io context id's.
 *
 * When a snapshot is taken the map between kernel and payload context id's is stored in a
 * NT_KM_IOCONTEXTS elf note.  The resumed snapshot recovers the map from that same note.
 * The note also contains the km io context id counter so the resumed snapshot should not
 * resuse an existing id.
 */

#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/aio_abi.h>

#include "bsd_queue.h"
#include "km.h"
#include "km_coredump.h"

struct km_iocontext {
   unsigned int kioc_maxevents;
   aio_context_t kioc_kcontext;
   aio_context_t kioc_pcontext;
};
typedef struct km_iocontext km_iocontext_t;

pthread_mutex_t km_iocmutex = PTHREAD_MUTEX_INITIALIZER;
size_t km_iocn;            // number of entries in the table km_iocp points to
km_iocontext_t* km_iocp;   // map of kernel context id's and payload context id's
aio_context_t km_piocontext = ((unsigned long)'k' << 56) | ((unsigned long)'m' << 48);

// Called when a payload context is being recovered due to a snapshot resume.
int km_iocontext_recover(unsigned int nr_events, aio_context_t pcontext)
{
   aio_context_t iocontext = 0;
   int rc = syscall(SYS_io_setup, nr_events, &iocontext);
   if (rc != 0) {
      return errno;
   }
   km_mutex_lock(&km_iocmutex);
   km_iocontext_t* t = realloc(km_iocp, (km_iocn + 1) * sizeof(km_iocontext_t));
   if (t == NULL) {
      km_mutex_unlock(&km_iocmutex);
      if (syscall(SYS_io_destroy, iocontext) != 0) {
         // Can't allocate memory and can't deallocate the iocontext!
         km_warn("io_destroy() failed, io context 0x%lx orphaned", iocontext);
      }
      return ENOMEM;
   }
   t[km_iocn].kioc_maxevents = nr_events;
   t[km_iocn].kioc_kcontext = iocontext;
   t[km_iocn].kioc_pcontext = pcontext;
   km_iocp = t;
   km_iocn++;
   km_mutex_unlock(&km_iocmutex);
   return 0;
}

// Called when a payload is creating a new io context.
// How to handle kernel reuse of an io context that the payload was using????
int km_iocontext_add(unsigned int nr_events, aio_context_t* pcontextp)
{
   aio_context_t piocontext = __atomic_add_fetch(&km_piocontext, 1, __ATOMIC_SEQ_CST);
   *pcontextp = piocontext;
   return km_iocontext_recover(nr_events, piocontext);
}

// Removed the passed payload iocontext from the xlate table
// We assume the caller will remove the io context
int km_iocontext_remove(aio_context_t pcontext)
{
   size_t i;
   km_mutex_lock(&km_iocmutex);
   for (i = 0; i < km_iocn; i++) {
      if (km_iocp[i].kioc_pcontext == pcontext) {
         int rv = syscall(SYS_io_destroy, km_iocp[i].kioc_kcontext);
         if (rv < 0) {
            // We know the context id is valid but we can't delete it?
            km_warn("Can't delete io context id 0x%lx, kernel context id: 0x%lx",
                    km_iocp[i].kioc_pcontext,
                    km_iocp[i].kioc_kcontext);
            return errno;
         }
         // We don't shrink the memory area when freeing an io context.
         memcpy(&km_iocp[i], &km_iocp[i + 1], (km_iocn - i - 1) * sizeof(km_iocontext_t));
         km_iocn--;
         km_mutex_unlock(&km_iocmutex);
         return 0;
      }
   }
   km_mutex_unlock(&km_iocmutex);
   return EINVAL;
}

// translate a kernel io context to a payload io context.
// I don' think we will need this function.
// km_iocontext_xlate_k2p()

// Translate a payload io context to a kernel io context.
int km_iocontext_xlate_p2k(aio_context_t pcontext, aio_context_t* kcontext)
{
   size_t i;
   km_mutex_lock(&km_iocmutex);
   for (i = 0; i < km_iocn; i++) {
      if (km_iocp[i].kioc_pcontext == pcontext) {
         *kcontext = km_iocp[i].kioc_kcontext;
         km_mutex_unlock(&km_iocmutex);
         return 0;
      }
   }
   km_mutex_unlock(&km_iocmutex);
   // Unknown payload io context
   return EINVAL;
}

// Figure out how much space the io context elf notes will need
size_t km_fs_iocontext_notes_length(void)
{
   return km_note_header_size(KM_NT_NAME) + sizeof(km_nt_iocontexts_t) +
          (km_iocn * sizeof(km_nt_iocontext_t));
}

// Write the io context elf note into buf.
size_t km_fs_iocontext_notes_write(char* buf, size_t length)
{
   char* cur = buf;
   size_t remain = length;

   cur += km_add_note_header(cur,
                             remain,
                             KM_NT_NAME,
                             NT_KM_IOCONTEXTS,
                             km_fs_iocontext_notes_length() - km_note_header_size(KM_NT_NAME));
   km_nt_iocontexts_t* iocnote = (km_nt_iocontexts_t*)cur;
   iocnote->size = sizeof(km_nt_iocontexts_t) + (km_iocn * sizeof(km_nt_iocontext_t));
   iocnote->nr_contexts = km_iocn;
   iocnote->piocontext = km_piocontext;
   cur += sizeof(km_nt_iocontexts_t);
   for (int i = 0; i < km_iocn; i++) {
      iocnote->contexts[i].nr_events = km_iocp[i].kioc_maxevents;
      iocnote->contexts[i].iocontext = km_iocp[i].kioc_pcontext;
      cur += sizeof(km_nt_iocontext_t);
   }
   return cur - buf;
}

// Recover the io contexts used by the payload
int km_fs_recover_iocontexts(char* ptr, size_t length)
{
   int rc = 0;
   km_nt_iocontexts_t* iocontexts = (km_nt_iocontexts_t*)ptr;
   km_piocontext = iocontexts->piocontext;
   for (int i = 0; i < iocontexts->nr_contexts; i++) {
      rc = km_iocontext_recover(iocontexts->contexts[i].nr_events, iocontexts->contexts[i].iocontext);
      if (rc != 0) {
         rc = -rc;
         break;
      }
   }
   return rc;
}

void km_iocontext_init(void)
{
   km_iocn = 0;
   km_iocp = NULL;
}

void km_iocontext_deinit(void)
{
   free(km_iocp);
   km_iocn = 0;
}
