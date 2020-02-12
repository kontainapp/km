To run: `<km path> --putenv LD_LIBRARY_PATH=/usr/lib64:/lib64 ./nginx.kmd -g 'daemon off;'`

Currently, `nginx` requires the use of a known location to output files such
as logs and other things. The path is set at compile time at `/opt/nginx`
currently. If this dir is not readable by the current user, sudo will be
required.

Currently broken due to unsupported syscalls.
```
fork() failed while spawning "worker process" (95: Not supported)
km: Unimplemented hypercall 130 (rt_sigsuspend)
```