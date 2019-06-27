#include "km_hcalls.h"
#include "pthread_impl.h"
#include "stdio_impl.h"
#include "unistd.h"

extern int main(int argc, char** argv);

static void dummy_0()
{
}
weak_alias(dummy_0, __pthread_tsd_run_dtors);
weak_alias(dummy_0, __do_orphaned_stdio_locks);
weak_alias(dummy_0, _init);
weak_alias(dummy_0, _fini);

__attribute__((__weak__)) void* __dso_handle = (void*)&__dso_handle;

volatile size_t __attribute__((__weak__)) __pthread_tsd_size = 0;
void* __pthread_tsd_main[1] __attribute__((__weak__)) = {0};

extern weak hidden void (*const __init_array_start)(void), (*const __init_array_end)(void);
extern weak hidden void (*const __fini_array_start)(void), (*const __fini_array_end)(void);

hidden void __libc_start_init(void)
{
   _init();
   uintptr_t a = (uintptr_t)&__init_array_start;
   for (; a < (uintptr_t)&__init_array_end; a += sizeof(void (*)())) {
      (*(void (**)(void))a)();
   }
}

hidden void __libc_exit_fini(void)
{
   uintptr_t a = (uintptr_t)&__fini_array_end;
   for (; a > (uintptr_t)&__fini_array_start; a -= sizeof(void (*)())) {
      (*(void (**)())(a - sizeof(void (*)())))();
   }
   _fini();
}

/*
 * Common entry point used for both main() and pthread entry.
 * The purpose is to process return from the actual code (main or pthread entry) and pass to exit().
 * Entry point in ELF header, then in km as km_guest.km_ehdr.e_entry, placed there by km_load_elf.
 */
_Noreturn void __start_c__(long is_main_argc, char** argv)
{
   (void)__pthread_tsd_size;
   if (is_main_argc == 0) {
      struct pthread* self = __pthread_self();
      pthread_exit(self->start(self->start_arg));
   } else {
   	__environ = argv + is_main_argc + 1;
      __libc_start_init();
      exit(main(is_main_argc, argv));
   }
}

/*
 * Common handler for all interrupts, exceptions and traps in the guest.
 * Since the monitor does all the heavy lifting, this function does not return directly.
 * If a return into the guest is required, the monitor will set everything up to return
 * the right place.
 */
_Noreturn void __km_handle_interrupt(void)
{
   km_hc_args_t args;
   while (1) {
      // Should not return
      km_hcall(HC_guest_interrupt, &args);
   }
}

_Noreturn void __km_handle_signal(
    uint64_t savsp, void (*sigact)(int, siginfo_t*, void*), int signo, siginfo_t* siginfo, void* uctx)
{
   km_hc_args_t args;
   args.arg1 = savsp;
   (*sigact)(signo, siginfo, uctx);
   while (1) {
      // Should not return
      km_hcall(SYS_rt_sigreturn, &args);
   }
}
