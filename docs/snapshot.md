# KM Snapshots

KM snapshot stops a KM guest and saves it's state in a file than can be restarted later. KM snapshot file are ELF format, so all of the `binutil` tools work with them.

## How to Create a Snapshot

How snapshots are created is a work in progress.

The test cases use a new KM hypercall, `HC_snapshot` that a guest calls to create a snapshot of itself and terminate. The snapshot can be restarted (see below). For example:

```
  km_hc_args_t snapshotargs = {};
  km_hcall(HC_snapshot, &snapshotargs);
```

Snapshots are named `kmsnap` by default. The `--snapshot=<file>` option overrides.

While this works for testing, is it insufficient for production. In particular, a production solution must allow a snapshot to be initiated from outside of the guest.

The details require a design. See Issue #571 for the work item.

## How to Run a Snapshot

The snapshot is an ELF file which is also runnable the same way as a regular executable.
Once a snapshot file has been created, the process can be restarted with the following command:

`km <snapshot file>`

A snapshot has the same guest memory, cpu and environment variables as the original process. The VDSO and KM runtime components of the guest image come from the `KM` instance running the snapshot.

Currently, file descriptors other than 0, 1, and 2 (`stdin`, `stdout`, and `stderr`) are not restored. See issue #572 for work item (includes sockets).

## Notes

Snapsshots introduce some new ELF PT_NOTES record types. Ideally these types would be defined in `elf.h`, but for now they are defined in `km/km_coredump.h`:
* `NT_KM_CPU` - KM specific VCPU status.
* `NT_KM_GUEST` - Information from `km_guest`.
* `NT_KM_DYNLINKER` - Information from `km_dynlinker`.

Snapshot recovery:
* Memory recovery from `PT_LOAD` and `PT_NOTES`(`NT_FILES`).
* VCPU recovery from `NT_NOTES`(`NT_PRSTATUS` and `NT_KM_VCPU`).
* VDSO and `runtime` built new in restored process.

* Snapshot has same environment as original process.
* https://lwn.net/Articles/495304/ (TCP_REPAIR)
* https://akkadia.org/drepper/tls.pdf (ELF support for Thread Local Storage)
