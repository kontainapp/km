# Java Under KM



* `https://www.infoq.com/articles/OpenJDK-HotSpot-What-the-JIT/`
* `https://openjdk.java.net/groups/hotspot/docs/RuntimeOverview.html`

## Building Java
Need mercurial (hg). Need OpenJDK
```
sudo dnf install java-openjdk-devel
sudo dnf install libXtst-devel libXt-devel libXrender-devel libXrandr-devel libXi-devel
sudo dnf install cups-devel
sudo dnf install fontconfig-devel
sudo dnf install alsa-lib-devel
```
From `payloads/java` run `make fromsrc` to get and build Java. (Note: takes a long time)

Build java.kmd:
```
../../tools/kontain-gcc -rdynamic -Wl,--rpath=/opt/kontain/lib64:/lib64:build/linux-x86_64-server-release/jdk/lib:build/linux-x86_64-server-release/jdk/lib/server -Wl,--hash-style=both -Wl,-z,defs -Wl,-z,noexecstack -Wl,-O1 -m64 -Wl,--allow-shlib-undefined -Wl,--exclude-libs,ALL -Wl,-rpath,\$ORIGIN -Wl,-rpath,\$ORIGIN/../lib -Ljdk/build/linux-x86_64-server-release/support/modules_libs/java.base -o jdk/build/linux-x86_64-server-release/support/native/java.base/java_objs/java.kmd jdk/build/linux-x86_64-server-release/support/native/java.base/java/main.o -ljli -lpthread -ldl

```

Run:
```
cd jdk
../../../build/km/km --dynlinker=../../../build/runtime/libc.so --putenv="LD_LIBRARY_PATH=/opt/kontain/lib64:/lib64:./build/linux-x86_64-server-release/jdk/lib/" ./build/linux-x86_64-server-release/support/native/java.base/java_objs/java.kmd Hello
```

## Obsolete

```
hg clone  http://hg.openjdk.java.net/jdk/jdk
```

In order to run the Java test suite need `JTReg`. Easiest way is the pre-compiled version from `https://ci.adoptopenjdk.net/view/Dependencies/job/jtreg/` (`.tar.gz`).

```
cd jdk
bash configure --enable-headless-only --disable-warnings-as-errors --with-native-debug-symbols=internal --with-jvm-variants=server --with-zlib=bundled --with-jtreg=<jtreg directory>
make
```

Assumes jdk is a subdirectory sibling to km.

Link Java Launcher:
```

#!/bin/bash

BUILD=build/linux-x86_64-server-release
WORKSPACE=/home/serge/workspace

# /bin/gcc -Wl,--hash-style=both -Wl,-z,defs -Wl,-z,noexecstack -Wl,-O1 -m64 -Wl,--allow-shlib-undefined -Wl,--exclude-libs,ALL -Wl,-rpath,\$ORIGIN -Wl,-rpath,\$ORIGIN/../lib -L${WORKSPACE}/jdk/build/linux-x86_64-server-release/support/modules_libs/java.base -o ${WORKSPACE}/jdk/build/linux-x86_64-server-release/support/native/java.base/java_objs/java ${WORKSPACE}/jdk/build/linux-x86_64-server-release/support/native/java.base/java/main.o -lz -ljli -lpthread -ldl

${WORKSPACE}/km/tools/kontain-gcc -shared -Wl,--hash-style=both -Wl,-z,defs -Wl,-z,noexecstack -Wl,-O1 -m64 -Wl,--allow-shlib-undefined -Wl,--exclude-libs,ALL -Wl,-rpath,\$ORIGIN -Wl,-rpath,\$ORIGIN/../lib -L${WORKSPACE}/jdk/build/linux-x86_64-server-release/support/modules_libs/java.base -o ${WORKSPACE}/jdk/build/linux-x86_64-server-release/support/native/java.base/java_objs/java.so ${WORKSPACE}/jdk/build/linux-x86_64-server-release/support/native/java.base/java/main.o -ljli -lpthread -ldl

${WORKSPACE}/km/tools/kontain-gcc -rdynamic -Wl,--rpath=/opt/kontain/lib64:/lib64:build/linux-x86_64-server-release/jdk/lib:build/linux-x86_64-server-release/jdk/lib/server -Wl,--hash-style=both -Wl,-z,defs -Wl,-z,noexecstack -Wl,-O1 -m64 -Wl,--allow-shlib-undefined -Wl,--exclude-libs,ALL -Wl,-rpath,\$ORIGIN -Wl,-rpath,\$ORIGIN/../lib -L${WORKSPACE}/jdk/build/linux-x86_64-server-release/support/modules_libs/java.base -o ${WORKSPACE}/jdk/build/linux-x86_64-server-release/support/native/java.base/java_objs/java.kmd ${WORKSPACE}/jdk/build/linux-x86_64-server-release/support/native/java.base/java/main.o -ljli -lpthread -ldl

# ${WORKSPACE}/km/tools/kontain-gcc -shared -Wl,--hash-style=both -Wl,-z,defs -Wl,-z,noexecstack -Wl,-O1 -m64 -Wl,--allow-shlib-undefined -Wl,--exclude-libs,ALL -Wl,-rpath,\$ORIGIN -Wl,-rpath,\$ORIGIN/../lib -L${WORKSPACE}/jdk/build/linux-x86_64-server-release/support/modules_libs/java.base -o ${WORKSPACE}/jdk/build/linux-x86_64-server-release/support/native/java.base/java_objs/java ${WORKSPACE}/jdk/build/linux-x86_64-server-release/support/native/java.base/java/main.o -lz ./build/linux-x86_64-server-release/jdk/lib/libjli.so -lpthread -ldl

cp ${WORKSPACE}/jdk/build/linux-x86_64-server-release/support/native/java.base/java_objs/java.so ${WORKSPACE}/jdk/build/linux-x86_64-server-release/jdk/bin
cp ${WORKSPACE}/jdk/build/linux-x86_64-server-release/support/native/java.base/java_objs/java.kmd ${WORKSPACE}/jdk/build/linux-x86_64-server-release/jdk/bin
```

Run .kmd:

```
../km/build/km/km --dynlinker=../km/build/runtime/libc.so ./build/linux-x86_64-server-release/jdk/bin/java.kmd Hello
```

Run .so as before:

```
../km/build/km/km ../km/build/runtime/libc.so --library-path=/opt/kontain/lib64:/lib64:$cwd/build/linux-x86_64-server-release/jdk/lib:$cwd/build/linux-x86_64-server-release/jdk/lib/server -- build/linux-x86_64-server-release/support/native/java.base/java_objs/java.so Hello
```

`--putenv _JAVA_LAUNCHER_DEBUG=1` displays launcher information.