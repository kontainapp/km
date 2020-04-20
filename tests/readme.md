# Notes on build with alpine libs

We extract all necessary libs to build complex runtime from alpine. We do that because they are pre-built for musl.
We build stuff on fedora to avoid messing with nested containers inside of our buildenv containers.

* For native alpine executables we just point gcc/g++ to libs fetched from Alpine.
* For .km/.km we add our musl build (with hcalls) in front of alpine libs for gcc/g++

**IMPORTANT** We lock libs locations to /opt/kontain/{runtime,alpine-lib/usr/lib} via full paths in ELF 'interpreter' and RPATH for dynamic builds,a and 'gcc -B` flags for static (we actually use -B for dynamic too, but it does not hurt). See "About locatons" section below

## Prepare libraries

We build alpine-based container with libs and extract libs. At a minimum we need the following :

* apk add make git gcc bash musl-dev g++
* more for things like libffi-dev for python c interface

`prep_alpine_libs.sh` does the job. It is called BEFORE building buildenv-image in ./tests, so the alpine libs can end up in the image

## Set symlinks

We set a few symlinks to make gcc happy with all defaults

```sh
# libc.a -> libruntime.a           # we may want to rename going forward
# libc.musl-x86_64.so.1 -> libc.so # libstdc++ refers to this one
```

### buildenv-image on fedora

it now has 2 phases:

* build alpine image, extract libs to ./opt/kontain
* build fedora-based buidenv, add libs

**WE USE FIXED LOCATION /opt/kontain with ./alpine-lib and ./runtime subdirs***, to simplify the whole story

* **NOTE** for payloads (python/etc) - we build under Fedora JUST TO MAKE SURE WE HAVE TEST PATH for customer obj files linked with kontain-gcc/alpine libs. On individual cases (e.g nginx) we may just buuild payloads in alpine, to accelerate .km production

## Build of .km and .kmd

See kontain-gcc for vars and paths used to link Kontain unikernels (.km and .kmd) and alpine native executables (.native.km and .native.kmd) in our Fedora-based build environment.

.native.km and .native.kmd are just convenience suffixes for us, these are linux binaries and can be run as such - they are just linked against Alpine (musl-based) libs.

### Build .km (with hcalls) and .native.km (with syscalls)

kontain-gcc and kontain-g++ wrap the settings.


```sh
# info from tools/kontain-gcc:
#----
# -kontain|-alpine Which binaries to build. Default 'kontain'
# -kv              Print a few kontain-specific options and the final gcc command

# examples
# Build kontain .km/.km. (-kontain and -static are defaults)
kontain-gcc hello_test.o -o hello_test.km
kontain-gcc -kontain -dynamic hello_test.o -o hello_test.kmd
kontain-gcc hello_test.o -o hello_test.so
# build alpine executables
kontain-g++ -alpine var_storage_test.o  -o var_storage_test.native.km
kontain-gcc -dynamic -alpine hcallargs_test.o -o hcallargs_test.native.kmd libhelper.a

# '-kv' prints some kontain flags and final gcc command
kontain-g++ -kv -alpine var_storage_test.o  -o var_storage_test.native.km
```

#### gcc '-pthread' weirdness

We follow musl's lead and provide empty libpthread.so to make `gcc -pthread` and `-lpthread` happy regardless of platform and link type.

## About locations

### Explanations

There are 2 locations picked up when dynamic exec is loaded.

1. dynamic linker name (from INTERPRETER in ELF). It is needed for the code to run, and to locate stuff when in GDB
1. shared libs (NEEDED + RPATH i ELF, or LD_LIBRARY_PATH)

Dynamic linker full path ls what we need to have hard-coded in ELF.

So, we use /opt/kontain location and e hard-coding it into ELS interpeter and RPATH.

For static .km it does not matter, and for dynamic there has to be LOCKED location for dynamic linker (linux does the same), for both start and gdb. Once you do it for dynamic linker (libc.so in our case, we may want to symlink it to something like ld.kontain.libc.so just to clarify :-)), moving other stuff around does not help much, it's still locked to the location.

If I put build dir (e.g. `/home/msterin/workspace/km/build/runtime/libc.so`) in ELF interpreter, it's gonna be hard to start on other boxes or containers, and it's wronger than having `/opt/kontain/runtime/libc.so` there

For libs, it's slightly easier (**setvenv LD_LIBRARY_PATH** can help) but super error prone unless the location is locked.

As a result, shared libs are picked from locations 'RPATH' in ELF execs is pointing to ( /opt/kontain/{runtime,alpine-lib}), and it can be changed on runtime with LD_LIBRARY_PATH with or without separate 'install' phase. But libc.so HAS to be in /opt/kontain , so each dev build HAS to place it there

So while I don't like it, it seems the less painful and most reliable way of doing things - just landing stuff to /opt/kontain on build, and doing NS virtualization if individual containers do not want to step on each other.


### Practical results and things to know about /opt/kontain

* /opt/kontain/alpine-lib/* `MUST BE present` for runtime builds. It is created during buildenv-image creation, kept  and kept there and extracted to host during buildev-local-fedora target. **DO NOT FORGET TO RUN make -C buildenv-local-fedora**.
* `/opt/kontain/{runtime,km}` are created during ./runtime and ./km build correspondently, and saved on the host where build is running.
  * To support that, `make withddocker` an
  d make `test-withdocker` volume-mount the above folders
* payload "blank" containers behave similarly - they have all payload-specific stuff, including alpine-libs - but no `runtime`. It is volume-mounted from the build host
* `testenv` images are self contained, and have all libs (alpine-lib and runtime), as well as KM, inside the images
* `runenv` images have what needs runs in VM (and only that), i.e. the actual payload and libs (alpine-lib and runtime). KM runs outside of the VM and is volume-mounted on 'docker run'
  * At some point we may put alpine-libs and runtime on host just as a size/update optimization technique, but then we'd need to add version to file paths
* On production Kubernetes, kontaind installation process lands bin/km on the box so runenv-image can run by having bin/km volume-mounted.
* Dynamic executables in their ELF info have hardcoded interpreter and RPATH pointing to /opt/kontain.

## TODO and Issues

### TODO

NOW

* recover buildenv-image tag in azure pipelines to latest (from alpine_libs)
* fix and enable demo-dweb - Maybe just revert to Aline:3.10 or Alpine:3.8 ? Fails with a mix  between ubuntu gcc /ninutils/elfutils and latest alpine stuff
* drop useless ls -L and trace in build.yaml and pipeline


LATER

* Place runtime and alpine-libs in ALL test and run environment
  * try to put only NEEDED libraries
  * remove -v from testenv and runenv usage - and only mount BIN for testenv
* review install of the above on kontaind/kuberneres as optimization (see kontaind/installer)
* reconcile docker run option s to test/run/build env - we seem to be repeating them multple times

### Issues

* Buildenv image ALONE is not enough for dockerized builds to work.
  * 'withdocker' need writeable /opt/kontain/runtime to export results
  * testenv and runenv (and CI) need `make -C tests .buildenv-local-lib` so libs are available

  Both issues are easy to cure, but for now they sty

*** END OF DOCUMENT ***
