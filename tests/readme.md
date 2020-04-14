# Notes on build with alpine libs

We extract all necessary libs to build complex runtime from alpine. We do that because they are pre-built for musl.
We build stuff on fedora to avoid messing with nested containers inside of our buildenv containers.

* For native alpine executables we just point gcc/g++ to libs fetched from Alpine.
* For .km/.km we add our musl build (with hcalls) in front of alpine libs for gcc/g++

**IMPORTANT** We lock libs locations to /opt/kontain/{runtime,alpine-lib/usr/lib} via full paths in ELF 'interpreter' and RPATH for dynamic builds,a and 'gcc -B` flags for static (we actually use -B for dynamic too, but it does not hurt)

## Prepare libraries

We build alpine-based container with libs and extract libs. At a minimum we need the following :

* apk add make git gcc bash musl-dev g++
* more for things like libffi-dev for python c interface

`pre_alpine_libs.sh` does the job. It is also called in ./tests BEFORE building buildenv-image, so the alpine libs end up in the image

## Set symlinks

We set a few symlinks to make gcc happy with all defaults
# libc.a -> libruntime.a           # we may want to rename going forward
# libc.musl-x86_64.so.1 -> libc.so # libstdc++ refers to this one

### buildenv-image on fedora

it now has 2 phases:

* build alpine image, extract libs to ./opt/kontain
* build fedora-based buidenv, add libs

**WE USE FIXED LOCATION /opt/kontain with ./alpine-lib and ./runtime subdirs, to simplify the whole story**

* **NOTE** for payloads (python/etc) - we build under Fedora JUST TO MAKE SURE WE HAVE TEST PATH for customer obj files linked with kontain-gcc/alpine libs. On individual cases (e.g nginx) we may just buuild payloads in alpine, to accelerate .km production

## Build of .km and .kmd

See kontain-gcc for vars and paths used to link Kontain unikernels (.km and .kmd) and alpine native executables (.native.km and .native.kmd) in our Fedora-based build environment

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

###### -pthread weirdness

We follow musl's lead and provide empty libpthread.so to make -pthread and -lpthread flags happy

*** END OF DOCUMENT ***
