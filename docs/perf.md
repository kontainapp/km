# How to collect perf statistics and stap profiles

## Dependencies and prerequisites

### Install perf tool:

```
sudo dnf install perf.x86_64 systemtap.x86_64
```

### Kernel and libraries debuginfo

See https://dnf-plugins-core.readthedocs.io/en/latest/debuginfo-install.html

```
sudo dnf debuginfo-install kernel glibc elfutils zlib
```

Also for stap

```
sudo dnf install kernel-devel.x86_64
```

Note that the debuginfo doesn't get automatically updated when the packages are updated with
`dnf upgrade`. To upgrade all debuginfo packages:

```
dnf upgrade --enablerepo="*"-debuginfo "*-debuginfo"
```

### FlameGraph visualizer

```
git clone https://github.com/brendangregg/FlameGraph FGDIR
```

## Record perf data and create flame graph

```
sudo perf record -F 1000 --call-graph dwarf -o perf.data -- ../build/km/km cperf_test.km
sudo chown `id -u` perf.data
perf script -i perf.data | FGDIR/stackcollapse-perf.pl | FGDIR/flamegraph.pl > perf.svg
```

The `perf.svg` file can be viewed in google chrome. It is explained in http://www.brendangregg.com/FlameGraphs/cpuflamegraphs.html. Basically it shows call stack with the width of the bars proportional to CPU percentage spend in the function.

## stap

### To list the possible probes as well as variables available at the probe site:

```
stap -L 'module("kvm").statement("kvm_fast_pio@arch/x86/kvm/x86.c:6894")'
stap -L 'module("kvm").function("kvm_fast_pio")'
stap -L 'module("kvm").function("kvm_fast_pio").return'
```

Wildcard characters could be used to get all matches

```
stap -L 'module("kvm").function("kvm_*")'
```

or even

```
stap -L 'module("*").function("*pio*")'
```

The more they need to cover the longer it takes though.

### Scripts

Once the probes are identified need to write a script to get specific data. `tools/stap/count_hcalls.stp` script will count hypercalls made by the payload.
Run it like this for redis-server:

```
sudo ./tools/stap/count_hcalls.stp -c './build/km/km ../redis/src/redis-server.km'
```

After the run is complete, see the result. ^C shows because the server was stopped with ^C.

```
...
1:M 10 Feb 2020 17:39:01.663 * Ready to accept connections
^C         clock_gettime(228) called	    485713 times
                    read(  0) called	    180980 times
                   write(  1) called	    180003 times
             epoll_pwait(281) called	     31136 times
              setsockopt( 54) called	      4508 times
                   fcntl( 72) called	      1810 times
               epoll_ctl(233) called	      1805 times
          rt_sigprocmask( 14) called	      1703 times
                   close(  3) called	       981 times
                  accept( 43) called	       920 times
             getpeername( 52) called	       901 times
               prlimit64(302) called	       564 times
                  getpid( 39) called	        97 times
                    open(  2) called	        81 times
                     brk( 12) called	        16 times
                    mmap(  9) called	        12 times
                  writev( 20) called	        11 times
                  munmap( 11) called	         4 times
            rt_sigaction( 13) called	         4 times
                mprotect( 10) called	         3 times
                   clone( 56) called	         3 times
                   futex(202) called	         3 times
                   ioctl( 16) called	         2 times
                  socket( 41) called	         2 times
                    bind( 49) called	         2 times
                  listen( 50) called	         2 times
              arch_prctl(158) called	         1 times
         set_tid_address(218) called	         1 times
                readlink( 89) called	         1 times
                 madvise( 28) called	         1 times
       sched_getaffinity(204) called	         1 times
                   fstat(  5) called	         1 times
                   readv( 19) called	         1 times
                   lseek(  8) called	         1 times
                    pipe( 22) called	         1 times
                  getcwd( 79) called	         1 times
                 sysinfo( 99) called	         1 times
           epoll_create1(291) called	         1 times
```

`tools/stap/kvm_time.stp` prints statistics for function calls in km and in the kernel:

```
sudo ./tools/stap/kvm_time.stp -c './build/km/km tests/hcperf_test.km 100000 1'
```

```
               func name:   avg ns [  min ns   max ns] - count
               handle_io:      333 [     311    26983] - 100003
   complete_fast_pio_out:      357 [     337    27503] - 100002
     km_vcpu_one_kvm_run:     5313 [    4996   214080] - 100003
            vmx_vcpu_run:      768 [     699    25288] - 100138
```

`tools/stap/vmx_functions.stp` traces function calls through `kvm/x86.c` and `kvm/vmx/vmx.c` files in the kernel. The leftmost column is nanoseconds since the last top level function call:

```
     0 vcpu-0(449380):   -> kvm_arch_vcpu_ioctl_run
   668 vcpu-0(449380):      -> kvm_arch_vcpu_load
  1353 vcpu-0(449380):         -> vmx_vcpu_load
  2023 vcpu-0(449380):            -> vmx_vcpu_load_vmcs
  2697 vcpu-0(449380):            <- vmx_vcpu_load_vmcs
  3357 vcpu-0(449380):         <- vmx_vcpu_load
  3963 vcpu-0(449380):      <- kvm_arch_vcpu_load
  4627 vcpu-0(449380):      -> kvm_load_guest_fpu
  5293 vcpu-0(449380):      <- kvm_load_guest_fpu
  5938 vcpu-0(449380):      -> kvm_set_cr8
  6570 vcpu-0(449380):      <- kvm_set_cr8
  7218 vcpu-0(449380):      -> complete_fast_pio_out
...
```
