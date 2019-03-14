# Handling of invalid memory references in KM and payloads

This document is a brief write up on what do we need to do / investigate and how to handle invalid memory references in KM.

[TOC]

## Reasons for invalid memory references

There are a few cases in KM life when (invalid) memory can be referenced:

1. init
1. running payload , i.e. being inside a KVM_RUN
1. executing hyper-call on behalf of a payload
1. code handling init KVM exits and dispatching hyper-calls
1. gdb access to misc. memory regions

Reasons:

* For init() , any SIGSEGV is a bug in KM.
* For payload running, invalid memory reference is a bug in the payload
  * it is exposed (currently) though KM_RUN exiting with KVM_SHUTDOWN (we will need to change that)
* For hyper-call handling, it is most likely a bug in payload memory allocation (e.g. a payload thread releases a memory than another thread is using). In which case, KM will get SIGSEGV.
* For exit handling code - it is KM bug.
* For gdb access to memory - it is the same as in hyper-call (most likely payload bug), or user bug (typing wrong address,which happens to step on some unmapped memory).

The above will be exacerbated when we implement MProtect for non-mapped but allocated regions in payload.

## Handling of invalid memory references

### Phase 1 - core dumps

At this phase, we do not support payload sigactions for SIGSEGV. This means that SIGSEGV should lead to immediate payload core dump (unless in gdb).  Work here is as follows:

* Find out how to differentiate SIGSEGV in payload (currently it is a generic SHUTDOWN), and add exit handler to KVM run, to generate payload coredump  and exit KM gracefully
* A SIGSEGV in KM should generate payload core dump and only then revert to standard linux behavior (km coredump)
  * This is needed *only* after we enter km_vcpu_run(), and *before* fini()
  * SIGSEGV is per-thread, so theoretically we can exit the thread and continue , or connect GDB.
    * The "exit the thread and continue" is really the payload decision - we will support it when we implement payload signals. For now this is not supported
    * Enable and start listening for gdb (subject to further configuration control) seems like a great helper. See [bitbucket issue](https://bitbucket.org/kontainapp/covm/issues/13/add-coredump-and-attach_to_running-support).
* Add test coverage

### GDB behavior

* When handling a memory access command in GDBstub, SIGSEGV should be caught and should lead to error returned to gdb client. No core dumps in this case
  * this is in addition to address checks against known memory layout - unless we want to validate against all mprotect()s and free_list() in mmaps … e.g. free_list() in mmaps will be mprotect()ed, and the memory there should not be visible to payload debugger, but the reaction should be "Invalid memory access" in gdb client
* in all other cases the behavior is the same as above

### Phase 2 - sigaction() in payload

It will be covered (much) later, after implementing signal handing for payload - but if there is no sighandler , the behavior will revert to the one above
