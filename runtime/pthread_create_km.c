/*
 * All of this file, more or less, came from musl. Thread create/exit logic is split between guest
 * and KM. Things visible and actively used in the guest are done here, for instance stdio, thread
 * linked list, tsd dtors, and such. KM manages the VCPU, stack, and such.
 */

#include "libc.h"
#include "pthread_impl.h"
#include "stdio_impl.h"

static void dummy_0()
{
}
weak_alias(dummy_0, __run_tls_dtors);
weak_alias(dummy_0, __pthread_tsd_run_dtors);
weak_alias(dummy_0, __do_orphaned_stdio_locks);

static void init_file_lock(FILE* f)
{
   if (f != NULL && f->lock < 0) {
      f->lock = 0;
   }
}

volatile int __thread_list_lock;
static int tl_lock_count;
static int tl_lock_waiters;

void __tl_lock(void)
{
   int tid = __pthread_self()->tid;
   int val = __thread_list_lock;
   if (val == tid) {
      tl_lock_count++;
      return;
   }
   while ((val = a_cas(&__thread_list_lock, 0, tid)))
      __wait(&__thread_list_lock, &tl_lock_waiters, val, 0);
}

void __tl_unlock(void)
{
   if (tl_lock_count) {
      tl_lock_count--;
      return;
   }
   a_store(&__thread_list_lock, 0);
   if (tl_lock_waiters)
      __wake(&__thread_list_lock, 1, 0);
}

void __tl_sync(pthread_t td)
{
   a_barrier();
   int val = __thread_list_lock;
   if (!val)
      return;
   __wait(&__thread_list_lock, &tl_lock_waiters, val, 0);
   if (tl_lock_waiters)
      __wake(&__thread_list_lock, 1, 0);
}

_Noreturn void __pthread_exit(void* result)
{
   pthread_t self = __pthread_self();
   sigset_t set;

   __pthread_tsd_run_dtors();
   __do_orphaned_stdio_locks();
   __run_tls_dtors();
   /*
    * The thread list lock must be AS-safe, and thus requires application signals to be blocked
    * before it can be taken.
    */
   __block_app_sigs(&set);
   __tl_lock();
   /*
    * If this is the only thread in the list, don't proceed with termination of the thread, but
    * restore the previous lock and signal state to prepare for exit to call atexit handlers.
    */
   if (self->next == self) {
      __tl_unlock();
      __restore_sigs(&set);
      exit(0);
   }
   /*
    * At this point we are committed to thread termination. Unlink the thread from the list.
    * This change will not be visible until the lock is released, which only happens after
    * SYS_exit has been called, via the exit futex address pointing at the lock.
    */
   libc.threads_minus_1--;
   self->next->prev = self->prev;
   self->prev->next = self->next;
   self->prev = self->next = self;
   __tl_unlock();
   __restore_sigs(&set);
   while (1) {
      __syscall1(SYS_exit, result);
   }
}

int __pthread_create(pthread_t* restrict res,
                     const pthread_attr_t* restrict ap,
                     void* (*entry)(void*),
                     void* restrict arg)
{
   pthread_t self = __pthread_self();
   unsigned long rc;
   sigset_t set;
   extern volatile long __start_thread__;   // need this to create reference
   (void)__start_thread__;

   if (libc.threaded == 0) {
      /*
       * We get here only once in a payload life time, when (and if) main thread calls the very
       * first pthread_create, hence there is nobody to race with.
       * After we convert all files open to this ploint, the newly created ones are handled
       * correctly because we set libc.threaded.
       */
      libc.threaded = 1;
      for (FILE* f = *__ofl_lock(); f != NULL; f = f->next) {
         init_file_lock(f);
      }
      __ofl_unlock();
      init_file_lock(__stdin_used);
      init_file_lock(__stdout_used);
      init_file_lock(__stderr_used);
      if (self->tsd == NULL) {
         self->tsd = (void**)__pthread_tsd_main;
      }
   }
   __block_app_sigs(&set);
   __tl_lock();
   libc.threads_minus_1++;
   rc = __syscall4(HC_pthread_create, res, ap, entry, arg);
   if (rc < 0) {
      libc.threads_minus_1--;
   }
   __tl_unlock();
   __restore_sigs(&set);
   return -rc;
}

weak_alias(__pthread_create, pthread_create);
weak_alias(__pthread_exit, pthread_exit);
