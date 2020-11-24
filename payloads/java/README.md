# Building Java 11 Under KM

Run `make -C payloads/java fromsrc` to get and build Java. This builds OpenJDK in  `payloads/java/(jdk-11.0.8+10)`. See `payloads/java/Makefile` for details.

The KM version of the java interpreter is `payloads/java/jdk-11.0.8+10/build/linux-x86_64-normal-server-release/images/jdk/bin/java.kmd`. To run it:

```
cd jdk-11.0.8+10
../../../build/km/km  --putenv="LD_LIBRARY_PATH=$(pwd)/build/linux-x86_64-normal-server-release/jdk/lib/server:$(pwd)/build/linux-x86_64-normal-server-release/jdk/lib/jli:$(pwd)/build/linux-x86_64-normal-server-release/jdk/lib:/opt/kontain/lib64:/lib64" $(pwd)/build/linux-x86_64-normal-server-release/images/jdk/bin/java.kmd --version
```

`make runenv-image` creates a self contained Docker image with KM Java.

`make validate-runenv-image` tests the Docker image.

The disassembly library, used for `hs_err` files:

```
cd payloads/java/jdk-11.0.8+10/src/utils/hsdis
# Get https://ftp.gnu.org/gnu/binutils/binutils-2.19.1.tar.bz2
# Untar to build/binutils-2.19.1
make BINUTILS=build/binutils-2.19.1/ CFLAGS="-Wno-error -fPIC" all64
cp 
```
## Java Tips and Tricks

* `--putenv _JAVA_LAUNCHER_DEBUG=1` displays launcher information.
* `java -Xint` runs interpretter only, no JIT. Useful for base testing.
* When Java crashes it (sometimes) creates a file called `hs_err_pid<pid>/log`. In our case pid=1.
* `-XX:+PrintFlagsFinal` displays all settable parameters.

Maven has respositories. Both Maven and Gradle use Maven repositories. We should too.

# Java Platform Module System (JPMS)

Project Jigsaw (https://openjdk.java.net/projects/jigsaw/) introduced a standardized module system (JPMS) to Java staring with Java 9. One goal of JPMS  is "Make it easier for developers to construct and maintain libraries and large applications". The Kontain API for Java leverages JPMS. In particular the API is delivered as the following files:
- `app.kontain.jmod` - used by customer when compiling their application with the KM API.

Temporary notes:
```
/*
 * From 'Getting Started' page: https://opemjdk.java.net/projects/jigsaw/quick-start
 *
 * javac -d mods/org.astro src/org.astro/module-info.java src/org.astro/org/astro/World.java 
 * javac --module-path mods -d mods src/com.greetings/module-info.java src/com.greetings/com/greetings/Main.java
 * jar --create --file=mlib/org.astro@1.0.jar --module-version=1.0 -C mods/org.astro .
 * jar --create --file=mlib/com.greetings.jar --main-class=com.greetings.Main -C mods/com.greetings .
 * java -p mlib -m com.greetings
 */
```

# KM API for Java 

The `app.kontain` directory contains the Java binding for the KM API. Currently the only thing exposed is taking a snapshot.

# Controlling Java Release Sizes

Modern JDK's don't come with a separate JRE anymore. The come with `jlink` which builds a Java runtime directory with a specified set of components. For example:
```
jlink --no-headers-files --no-man-pages --compress=2 --add-modules java.base,java.logging --output runtime
```

Builds a directory `runtime` with just the pieces needed for `java.base` and `java.logging`, resulting in a substantial space savings.


# Internals

### Java Memory Management and GC

* https://openjdk.java.net/jeps/270 - Stacks, yellow and red zones.
* https://betsol.com/java-memory-management-for-java-virtual-machine-jvm/
* https://shipilev.net/jvm/anatomy-quarks/

The OpenJDK Compressed OOPS feature does problematic things when it's `mmap(2)` hints are not honored. Use `-XX:-UseCompressedOops` to disable Compressed OOPS.

### Java Signal Handling

Java uses the `si_addr` field for `SIGSEGV` to implement the following features:
* Safepoint synchronization
* Stack yellow and red zones
* (anything else?)

## Useful Links

* `https://openjdk.java.net/groups/hotspot/docs/RuntimeOverview.html`
* `https://www.infoq.com/articles/OpenJDK-HotSpot-What-the-JIT/`

## Java Test Suite Notes

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
