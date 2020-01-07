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
../../tools/kontain-gcc -rdynamic -Wl,--rpath=/opt/kontain/lib64:/lib64:build/linux-x86_64-server-release/jdk/lib:build/linux-x86_64-server-release/jdk/lib/server -Wl,--hash-style=both -Wl,-z,defs -Wl,-z,noexecstack -Wl,-O1 -m64 -Wl,--allow-shlib-undefined -Wl,--exclude-libs,ALL -Wl,-rpath,\$ORIGIN -Wl,-rpath,\$ORIGIN/../lib -Ljdk/build/linux-x86_64-server-release/support/modules_libs/java.base -o jdk/build/linux-x86_64-server-release/jdk/bin/java.kmd jdk/build/linux-x86_64-server-release/support/native/java.base/java/main.o -ljli -lpthread -ldl

```

Run:
```
cd jdk
../../../build/km/km --dynlinker=../../../build/runtime/libc.so --putenv="LD_LIBRARY_PATH=/opt/kontain/lib64:/lib64:./build/linux-x86_64-server-release/jdk/lib/" ./build/linux-x86_64-server-release/jdk/bin/java.kmd Hello
```

## Testing

OpenJDK uses a tool called `jtreg` for testing. While `jtreg` can be built from source, that requires a installing bunch of dependencies. (See `https://openjdk.java.net/jtreg/build.html` for details).

The JDK 'Adoption Group' (`https://openjdk.java.net/jtreg/build.htmc`) publishes prebuilt tarballs (`https://ci.adoptopenjdk.net/view/Dependencies/job/jtreg/`). That's what we use (`jtreg-4.2-b16.tar.gz`).

`jtreg` compiles the source for test program(s), runs the test(s), and deletes the object files (`.class` and `.jar`).




For example:

```
$ ../jtreg/bin/jtreg -verbose:all -testjdk:build/linux-x86_64-server-release/jdk/ -compilejdk:build/linux-x86_64-server-release/jdk/ test/jdk/java/util/Random/NextBytes.java
--------------------------------------------------
TEST: java/util/Random/NextBytes.java
TEST JDK: /home/muth/kontain/km/payloads/java/jdk/build/linux-x86_64-server-release/jdk

ACTION: build -- Passed. All files up to date
REASON: Named class compiled on demand
TIME:   0.001 seconds
messages:
command: build NextBytes
reason: Named class compiled on demand
elapsed time (seconds): 0.001

ACTION: main -- Passed. Execution successful
REASON: Assumed action based on file name: run main NextBytes 
TIME:   0.143 seconds
messages:
command: main NextBytes
reason: Assumed action based on file name: run main NextBytes 
Mode: othervm
elapsed time (seconds): 0.143
configuration:
STDOUT:

Passed = 12, failed = 0

STDERR:
STATUS:Passed.
rerun:
cd /home/muth/kontain/km/payloads/java/jdk/JTwork/scratch && \
DISPLAY=:0 \
HOME=/home/muth \
LANG=en_US.UTF-8 \
PATH=/bin:/usr/bin:/usr/sbin \
XMODIFIERS=@im=ibus \
CLASSPATH=/home/muth/kontain/km/payloads/java/jdk/JTwork/classes/java/util/Random/NextBytes.d:/home/muth/kontain/km/payloads/java/jdk/test/jdk/java/util/Random:/home/muth/kontain/jtreg.binary/lib/javatest.jar:/home/muth/kontain/jtreg.binary/lib/jtreg.jar \
    /home/muth/kontain/km/payloads/java/jdk/build/linux-x86_64-server-release/jdk/bin/java \
        -Dtest.src=/home/muth/kontain/km/payloads/java/jdk/test/jdk/java/util/Random \
        -Dtest.src.path=/home/muth/kontain/km/payloads/java/jdk/test/jdk/java/util/Random \
        -Dtest.classes=/home/muth/kontain/km/payloads/java/jdk/JTwork/classes/java/util/Random/NextBytes.d \
        -Dtest.class.path=/home/muth/kontain/km/payloads/java/jdk/JTwork/classes/java/util/Random/NextBytes.d \
        -Dtest.vm.opts= \
        -Dtest.tool.vm.opts= \
        -Dtest.compiler.opts= \
        -Dtest.java.opts= \
        -Dtest.jdk=/home/muth/kontain/km/payloads/java/jdk/build/linux-x86_64-server-release/jdk \
        -Dcompile.jdk=/home/muth/kontain/km/payloads/java/jdk/build/linux-x86_64-server-release/jdk \
        -Dtest.timeout.factor=1.0 \
        -Dtest.root=/home/muth/kontain/km/payloads/java/jdk/test/jdk \
        com.sun.javatest.regtest.agent.MainWrapper /home/muth/kontain/km/payloads/java/jdk/JTwork/java/util/Random/NextBytes.d/main.0.jta

TEST RESULT: Passed. Execution successful
--------------------------------------------------
Test results: passed: 1
Report written to /home/muth/kontain/km/payloads/java/jdk/JTreport/html/report.html
Results written to /home/muth/kontain/km/payloads/java/jdk/JTwork

```
```

# Java Tips and Tricks

`--putenv _JAVA_LAUNCHER_DEBUG=1` displays launcher information.