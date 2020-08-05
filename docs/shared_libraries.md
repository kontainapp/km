# Shared Library Support

*Notes on KM shared library support.*

KM leverages the MUSL dynamic loader, `libc.so`. (MUSL combines the dynamic loader and `libc` into a single binary. GLIBC has separate binaries for the dynamic loader and and for `libc`).

Like the GLIBC dynamic loader (`ld.so`), the MUSL dynamic loader can be started from the command line. For example:

```
$ ../build/km/km --putenv LD_LIBRARY_PATH=/opt/kontain/lib64:/lib64 ../build/runtime/libc.so stray_test.so div0
```

## GBD and Core Files

Unlike static binaries where the virtual address in core file are the same as virtual address in the executable file, core files from dynamically loaded files need a translation between core file address and share object file addresses. Currently this translation is manual and uses the `info proc mappings` and `add-symbol-file` commands.

For example:
```
$ gdb ../build/runtime/libc.so kmcore
...
Reading symbols from ../build/runtime/libc.so...
[New LWP 1]
[New LWP 2]
#0  0x00007ffffafb0513 in ?? ()
[Current thread is 1 (LWP 1)]
(gdb) info proc mappings
Mapped address spaces:

          Start Addr           End Addr       Size     Offset objfile
            0x200000           0x2115c0    0x115c0        0x0 /home/muth/kontain/km/build/runtime/libc.so
            0x212000           0x2580be    0x460be    0x12000 /home/muth/kontain/km/build/runtime/libc.so
            0x259000           0x28d1b4    0x341b4    0x59000 /home/muth/kontain/km/build/runtime/libc.so
            0x28ec40           0x292440     0x3800    0x8d000 /home/muth/kontain/km/build/runtime/libc.so
      0x7ffffafae000     0x7ffffafb0000     0x2000        0x0 /home/muth/kontain/km/tests/stray_test.so
      0x7ffffafb0000     0x7ffffafb1000     0x1000     0x2000 /home/muth/kontain/km/tests/stray_test.so
      0x7ffffafb1000     0x7ffffafb2000     0x1000     0x3000 /home/muth/kontain/km/tests/stray_test.so
      0x7ffffafb2000     0x7ffffd5d8000  0x2626000     0x4000 /home/muth/kontain/km/tests/stray_test.so
(gdb) add-symbol-file stray_test.so -o 0x7ffffafae000
add symbol table from file "stray_test.so" with all sections offset by 0x7ffffafae000
(y or n) y
Reading symbols from stray_test.so...
```

Note: the `-o` value for `add-symbol-file` is the virtual address of offset 0 for the mapped file.

```

(gdb) bt
#0  0x00007ffffafb0513 in div0 (optind=2, argc=2, argv=0xffff81fd)
    at stray_test.c:110
#1  0x00007ffffafb0394 in main (argc=argc@entry=2,
    argv=argv@entry=0x7fffffdfde70) at stray_test.c:321
#2  0x0000000000214a8a in libc_start_main_stage2 (main=0x7ffffafb0210 <main>,
    argc=2, argv=0x7fffffdfde70) at musl/src/env/__libc_start_main.c:94
#3  0x00007ffffafb0414 in _start ()
#4  0x0000000000000003 in ?? ()
#5  0x0000000000000002 in ?? ()
#6  0x00007fffffdfdfbd in ?? ()
#7  0x00007fffffdfdfcb in ?? ()
#8  0x0000000000000000 in ?? ()
(gdb)
```

## C++ Support

Goal: Compile gcc locally so we have control over all the runtime pieces.

Prerequisites:
  * gmp-devel
  * mpfr-devel
  * libmpc-devel
  * isl-devel
  * flex

Building:
We have a fork of gcc that we made changes to: `https://github.com/kontainapp/gcc.git`
We use the `gcc-9_2_0-kontain` branch.

```
$ git clone https://github.com/kontainapp/gcc.git
$ cd gcc
$ git checkout gcc-9_2_0-kontain
$ ./configure --prefix=/opt/kontain --enable-clocale=generic \
    --disable-bootstrap --enable-languages=c,c++ --enable-threads=posix \
    --enable-checking=release --disable-multilib --with-system-zlib \
    --enable-__cxa_atexit  --disable-libunwind-exceptions --enable-gnu-unique-object \
    --enable-linker-build-id --with-gcc-major-version-only \
    --with-linker-hash-style=gnu --enable-plugin --enable-initfini-array \
    --with-isl --without-cuda-driver  \
    --enable-gnu-indirect-function --enable-cet --with-tune=generic
$ make -j
$ sudo make install
```

## kontain-gcc

Only really works with `.o` files created with default gcc. For example: `kontain-gcc -o t.km t.c` fails, but `gcc -c t.c; kontain-gcc -o t.km t.o` works.

## PT_INTERP

Elf files of type ET_EXEC optionally contain a PT_INTERP region. A PT_INTERP region contains the path of the dynamic loader to be used for the program.

`cc -fPIC -c t.c; ../tools/bin/kontain-gcc -rdynamic -o t.km t.o`

For CPP:

`./build/km/km --dynlinker=../build/runtime/libc.so var_storage_test.kmd`