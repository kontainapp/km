# Signal Handling in KM Guests

**Note: This is a work in progress.**

Reference(s):

* [IEEE Std 1003.1/Open Group Base Specifications (Issue 7) http://pubs.opengroup.org/onlinepubs/9699919799/](http://pubs.opengroup.org/onlinepubs/9699919799/)
* [The GNU C Library: 24. Signal Handling - https://www.gnu.org/software/libc/manual/html_node/Signal-Handling.html](https://www.gnu.org/software/libc/manual/html_node/Signal-Handling.html)
* [Linux Standards Base (current: LSB 5) - https://refspecs.linuxfoundation.org/lsb.shtml](https://refspecs.linuxfoundation.org/lsb.shtml)
* [Debugging Using DWARF http://www.dwarfstd.org/doc/Debugging%20using%20DWARF.pdf](http://www.dwarfstd.org/doc/Debugging%20using%20DWARF.pdf)
* [DWARF Debugging Information Format http://www.dwarfstd.org/doc/DWARF5.pdf](http://www.dwarfstd.org/doc/DWARF5.pdf)

## Overview
Signals are a messy part of the Linux programming environment. They were messy in original UNIX and they have gotten messier over time. One area where this messiness is obvious is the mapping between section 2 `libc` functions and native Linux systems calls. Unlike most section 2 `libc` functions where there is direct correspondence between `libc` and the system call, many signal oriented functions have nontrivial mappings between the function and the system call. See the implementation of `sigaction(2)` for a good example of this.

The following table shows how some signal-oriented native Linux syscalls map into `libc` functions.

| Linux SysCall | `libc` | Description |
| ------- | ---- | --------|
| SYS_rt_sigprocmask | pthread_sigmask(3)<br>sigaction(2) | Signal masking. In MUSL, both process-wide masking and<br>per-thread masking use the common system call. |
| SYS_rt_sigaction | signal(2)<br>sigaction(2)| Define a signal handler. |
| SYS_rt_sigtimedwait | sigtimedwait(2)<br>sigwait(2)<br>sigwaitinfo(2) | Wait for signal(s) to occur. |
| SYS_rt_sigpending | sigpending(2) | Nondestructively get pending signals. |
| SYS_signalfd4 | signalfd(2) | Create file descrptor to recieve signals |
| SYS_kill | kill(2) | Send a signal to a process. |
| SYS_tkill | abort(3)<br>raise(3)<br>pthread_cancel(3)<br>pthread_kill(3)| Send a signal to a thread. |

There are three distinct methods defined in the Linux environment for a process to receive signals:

* rt_sigtimed wait(2) and related functions (related functions implemented in libc).
* signalfd(2) (similar to rt_sigtimed_wait, but can be used by poll/select/epoll).
* Asynchronous callback (classic signals).

## Guest Signal Architecture

KM emulates what the Linux kernel would do for a process WRT signals. In particular:

* KM maintains a set of signal handler dispositions (Default, Ignore, Handler) for the guest.
* KM maintains per-thread signal mask for the the guest.
* KM maintains per-thread and process-wide pending signal lists for the guest.
* KM translates faults and exceptions that occur in the guest into signals for the guest.
* KM implements signal default actions (like Terminate and Terminate-with-Core()).

The design goal for KM signal handling is compatibility with what the Linux kernel would do, within the bounds of the following restrictions:

1. A KM guest process can only send signals to itself. For example kill(0, sig), raise(sig). EINVAL is returned if this restriction is violated.
2. A KM guest process can only receive signals generated with the KM environment:
  * Explicitly sent by the guest process itself.
  * Created by KM in response to an exception or fault that occurs within the guest process. For example, SIGSEGV.

When KM wants to invoke a signal handler defined by the guest process, KM first saves the guest's registers to the stack (in guest memory) and sets up the guest to run the user defined handler function. The guest stack has a return address that points to a KM runtime routine. When the guest-defined signal handler returns, the routine makes a KM hypercall to inform KM that the guest signal handler has completed.

## Guest Exception Handling

When runtime errors such as divide by zero or a stray memory reference occur in a KVM guest, they result in a X86 interrupt seen in the guest. In order to catch these errors, the Kontain runtime contains an interrupt handler. All this exception handler does is make a hypercall. The hypercall handler inside KM takes care of the rest.

At guest initialization time, KM creates an Interrupt Descriptor Table (IDT) in guest memory. The IDT points at the runtime exception handler. The KVM API KVM_GET_VCPU_EVENTS function exposes the reason for the interrupt.

All of the intelligence for dealing with the interrupt resides in the monitor.

## Core Files
* [System V ABI - https://wiki.osdev.org/System_V_ABI](https://wiki.osdev.org/System_V_ABI)
* [Anatomy of an ELF core file - https://www.gabriel.urdhr.fr/2015/05/29/core-file/](https://www.gabriel.urdhr.fr/2015/05/29/core-file/)

Coredumps are ELF format files with header type ET_CORE.
Unlike object files and executable files, coredump files only contain an ELF header, a single PT_NOTE program header area, and one or more PT_LOAD program header areas.

The PT_NOTE area contains lots of information about the process the core describes. See below for details.

The PT_LOAD areas describe
As (with data). core dump only contains a program header table (Elf64_Phdr) with the following entries:

* A single PT_NOTE section. (contents described below).
* One PT_LOAD section for each mmap'ed region in the process. Note: a minimal km image, and by extension coredump, contain 6 mmap'ed regions:
..* .text from executable.
..* .data from executable.
..* .bss from executable.
..* Global Descriptor Table (GDT).
..* Interrupt Descriptor Table (IDT).
..* Stack for root thread.

Will be more PT_LOAD headers if the guest process starts threads or creates it's own mmaps.

### PT_NOTE Section

Ideally, KM should create coredump for guest processes that are compatable from
coredumps created by the Linux kernel.

This is `readelf -n` output for a corefile generated by `stray_test` running on the linux kernel:

```{}
Displaying notes found at file offset 0x000002a8 with length 0x00000a78:
  Owner                 Data size	Description
  CORE                 0x00000150	NT_PRSTATUS (prstatus structure)
  CORE                 0x00000088	NT_PRPSINFO (prpsinfo structure)
  CORE                 0x00000080	NT_SIGINFO (siginfo_t data)
  CORE                 0x00000140	NT_AUXV (auxiliary vector)
  CORE                 0x00000114	NT_FILE (mapped files)
    Page size: 4096
                 Start                 End         Page Offset
    0x0000000000400000  0x0000000000401000  0x0000000000000000
        /home/muth/kontain/covm/tests/stray_test
    0x0000000000401000  0x0000000000481000  0x0000000000000001
        /home/muth/kontain/covm/tests/stray_test
    0x0000000000481000  0x00000000004a6000  0x0000000000000081
        /home/muth/kontain/covm/tests/stray_test
    0x00000000004a7000  0x00000000004ad000  0x00000000000000a6
        /home/muth/kontain/covm/tests/stray_test
  CORE                 0x00000200	NT_FPREGSET (floating point registers)
  LINUX                0x00000340	NT_X86_XSTATE (x86 XSAVE extended state)

```

It appears that the most important section is NT_PRSTATUS.

```{c}
struct elf_siginfo {
	int si_signo;
	int si_code;
	int si_errno;
};

struct elf_prstatus {
	struct elf_siginfo pr_info;
	short int pr_cursig;
	unsigned long int pr_sigpend;
	unsigned long int pr_sighold;
	pid_t pr_pid;
	pid_t pr_ppid;
	pid_t pr_pgrp;
	pid_t pr_sid;
	struct timeval pr_utime;
	struct timeval pr_stime;
	struct timeval pr_cutime;
	struct timeval pr_cstime;
	elf_gregset_t pr_reg;
	int pr_fpvalid;
};
```

The only thing that need to be populated for GDB to work are the reisters `pr_reg`.

There is one NT_PRSTATUS section per running thread.