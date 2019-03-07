#!/bin/bash

MUSL_TOP="../km/runtime/musl"

gcc -static -nostdlib ${MUSL_TOP}/lib/crt1.o -o python Programs/python.o libpython3.7m.a ${MUSL_TOP}/lib/libc.a
