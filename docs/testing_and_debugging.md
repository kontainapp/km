# Testing

Each PR has to be associated with an automated test, and analyzed with code coverage.

**Tests are residing in ./tests and built and started by a Makefile there.** Makefile calls `km_core_tests.bats` which is what controls test runs.

## Test tools

We use `BATS` to coordinate and glue together multiple test suites, and `silentbycycle/greatest` for C test suits support. Both tools are configured as git submodules under ./test, so `git submodule update --init` is needed after first `git clone` on KM repo.

* [BATS](https://github.com/bats-core/bats-core) (or `dnf install bats`)
  * A good all around way to organize multiple test executable.
  * TAP-compliant testing framework for Bash
  * allows to scan directories / runs multiple tests(named `<test>.bats`)
* [greatest](https://github.com/silentbicycle/greatest)
  * A single .h file set of macros / C  / CLI
  * Good review (and comparison with others) [here](https://spin.atomicobject.com/2013/07/31/greatest-c-testing-embedded/)
  * Does almost all we  want (has a few shortcomings but nothing major - i.e. does not do parametrized tests, does not support test-specific setup/teardown - only per suite, etc), and very simple to install/use in the payloads.

We also use bats-assert helpers, see https://github.com/ztombol/bats-assert or tests/bats-assert/README.md for details

## Code coverage

We use gcov/gcovr for C test coverage. While `gcov` (the program generating readable coverage info from coverage binary files) is a part of gcc package, `gcovr` (the program doing nice html rendering with source code) needs to be installed, i.e. `sudo dnf install gcovr`. See [gcovr site](https://www.gcovr.com) for details

* To build coverage report, run `make coverage`. It will re-compile `km` with coverage (respecting settings in custom.mk) run the test suite and generate test coverage HTML reports. It will also print out the location of the reports.
* To clean up coverage artifacts, run `make covclean`.

## Test code layout and usage

Test code is a collection of .c and .cpp programs in ./tests, which are compiled for Linux and for Kontain, and then run under BATS framework, with validating results and checking (when needed) compliance with Linux results

Bash functions using BATS framework and validating test results are in `tests/km_core_tests.bats`.
This bats file can run a set of tests for different KM linking strategy (static, dynamic, shared), and expects misc environment vars and locations, so we do not invoke it directly, and instead always use `tests/run_bats_test.sh` bash script.

Run `cd tests; ./run_bats_tests.sh --help` for flags.

`make test` from top-level or ./tests also invokes `run_bats_test.sh`. Adding MATCH=regexp will only run tests with the description matching the *regexp* (*make* will pass `--match=regexp` to `run_bats_test.sh`).

Note that regexp in `make test MATCH=regexp` and `./tests/run_bats_tests.sh --match=regexp` only needs to match a substring of the test description string (i.e. the string after @test keyword), so for example, 'gdb' will match all tests with 'gdb' anywhere in the description.

### Writing tests

This is a general work flow. Use .c and .bats files in tests as examples.

* write C payload that does something and prints success/error messages and extra info
* add a test to km_core_tests.bats which calls the payload and checks the expected output using misc. assert functions (see docs for bats-assert)
  * the test description should include unique test name; and for convenience we also add the payload name to the test description string. See .bats file for examples
* If you need to skip your test from running for all or some of payload linkage, add test name to an exception list - i.e. `todo_*` or `not_needed_*` in `tests/km_core_tests.bats`.
* if your test needs to behave differently (e.g. you are looking at offsets or dyntables), `$test_type` bash variable keeps 'static' or 'dynamic' or 'so' - use to to check for correct results.

You can invoke individual tests or groups of tests via the helper script, e.g.:

```shell
  cd tests
  ./run_bats_tests.sh --pretty  --test-type=so --match=setup_link # one test only, 'shared' (.so) test only
  ./run_bats_tests.sh --match=setup_link                          # one test for all linkage types (static/dynamic/so)
  ./run_bats_tests.sh --match=setup                               # all setup* tests for all linkage types (static/dynamic/so)
  ./run_bats_tests.sh --pretty  --test-type=so                    # all tests for .so linkage type
  ./run_bats_tests.sh --pretty  --match=mem_regions --test-type=so  --dry-run # print env and command to invoke bats
```

**DO NOT FORGET TO RUN `make coverage` to validate your test code coverage**

## Debugging

To debug payload, run `km -g<port> payload.km` - this will start km listening on port for gdb client. KM will also print out a gdb command to run, which connects to km to debug.

```txt
tests/hello_test.km: Waiting for a debugger. Connect to it like this:
	gdb -q --ex="target remote work:2159" /home/paulp/ws/ws2/km/tests/hello_test.km
GdbServerStubStarted
```

Use the km command's --gdb-listen flag to have km listen for gdb client connections while the payload runs.

In Visual Studio Code, we provide `launch.json` with different debugging options. Choose from Debug drop down in Debug mode.

### Debugging fork-ed and exec-ed processes

To use gdb in conjunction with km payloads and their child processes you need to be aware of these rules:

- A child payload inherits km command line debug settings from the parent km, so if gdbstub is running in the parent km, it will be running in the child km,
- To stop a child payload after a fork() call returns, set the parent km's environment variable KM_GDB_CHILD_FORK_WAIT to the name of km payload that called fork() (or clone()), the value is a regular expression,
- To gain control in the child payload after an execve() completes use the gdb "catch exec" command before the execve() call is made by the child payload

The following is an example of debugging a simple program that forks and execs to another simple program.
The goal of the example is to set a breakpoint in the child payload and do some debugging.
Keep in mind that we have 3 terminal sessions in this example.  One session where we start the payload and view the output of the payloads.
Another session where we can debug the parent payload.
And a last session where we can debug the child payload.

Parent program gdb_forker_test.c:

```c
int main(int argc, char* argv[])
{
   char payload[] = "hello_test";
   pid_t pid;

   pid = fork();
   if (pid < 0) {
      fprintf(stderr, "fork() in %s failed, %s\n", argv[0], strerror(errno));
      return 1;
   } else if (pid == 0) {
      char* new_argv[2];
      new_argv[0] = payload;
      new_argv[1] = NULL;
      char* new_envp[1];
      new_envp[0] = NULL;
      fprintf(stderr, "Child pid %d exec()'ing to %s\n", getpid(), payload);
      execve(payload, new_argv, new_envp);
      fprintf(stderr, "execve() to %s, pid %d, failed %s\n", payload, getpid(), strerror(errno));
      return 1;
   } else { // parent
      fprintf(stderr, "Waiting for child pid %d to terminate\n", pid);
      pid_t waited_pid;
      int status;
      waited_pid = waitpid(pid, &status, 0);
      assert(waited_pid == pid);
      fprintf(stdout, "Child pid %d terminated with status %d (0x%x)\n", pid, status, status);
   }
   return 0;
}
```

Child program hello_test.c:

```c
static const char msg[] = "Hello,";

int main(int argc, char** argv)
{
   char* msg2 = "world";

   printf("%s %s\n", msg, msg2);
   for (int i = 0; i < argc; i++) {
      printf("%s argv[%d] = '%s'\n", msg, i, argv[i]);
   }
   exit(0);
}
```

Start the parent program, attach gdb and let the payload continue:

```sh
[paulp@work tests]$ export KM_GDB_CHILD_FORK_WAIT=".*gdb_forker.*"
[paulp@work tests]$ env | grep KM
[paulp@work tests]$ ../build/km/km -g  gdb_forker_test.km
19:05:35.707719 km_gdb_attach_messag 319  km      Waiting for a debugger. Connect to it like this:
	gdb -q --ex="target remote work:2159" /home/paulp/ws/ws2/km/tests/gdb_forker_test.km
GdbServerStubStarted
```

Start an instance of gdb that attaches to gdb_forker_test.km

```sh
[paulp@work km]$ gdb -q --ex="target remote work:2159"
Remote debugging using work:2159
Reading /home/paulp/ws/ws2/km/tests/gdb_forker_test.km from remote target...
warning: File transfers from remote targets can be slow. Use "set sysroot" to access files locally instead.
Reading /home/paulp/ws/ws2/km/tests/gdb_forker_test.km from remote target...
Reading symbols from target:/home/paulp/ws/ws2/km/tests/gdb_forker_test.km...
0x0000000000201116 in _start ()
(gdb) c
Continuing.
```

The parent program gdb_forker_test resumes, forks a child, and the child km wants another instance of gdb to attach to the child payload:

```sh
19:06:39.884242 km_gdb_accept_connec 255  km      Connection from debugger at 10.1.10.47
19:07:08.481122 km_gdb_attach_messag 319  1001.km      Waiting for a debugger. Connect to it like this:
	gdb -q --ex="target remote work:2160" /home/paulp/ws/ws2/km/tests/gdb_forker_test.km
GdbServerStubStarted

Waiting for child pid 1001 to terminate
```

Now the child instance of gdb_forker_test is waiting for another instance of gdb to attach to it before proceeding.
So we attach and find gdb is waiting after the fork() call in the child returns:

```sh
[paulp@work km]$ gdb -q --ex="target remote work:2160"
Remote debugging using work:2160
Reading /home/paulp/ws/ws2/km/tests/gdb_forker_test.km from remote target...
warning: File transfers from remote targets can be slow. Use "set sysroot" to access files locally instead.
Reading /home/paulp/ws/ws2/km/tests/gdb_forker_test.km from remote target...
Reading symbols from target:/home/paulp/ws/ws2/km/tests/gdb_forker_test.km...
__syscall0 (n=57) at ./syscall_arch.h:21
21	   return arg.hc_ret;
(gdb) bt
#0  __syscall0 (n=57) at ./syscall_arch.h:21
#1  fork () at musl/src/process/fork.c:21
#2  0x0000000000201262 in main (argc=1, argv=0x7fffffdfc838) at gdb_forker_test.c:38
(gdb)
```


Then we tell gdb we want to gain control when the execve() call in the gdb_forker_test child completes:

```sh
(gdb) catch exec
Catchpoint 1 (exec)
(gdb) c
Continuing.
Remote target is executing new program: /home/paulp/ws/ws2/km/tests/hello_test.km
Reading /home/paulp/ws/ws2/km/tests/hello_test.km from remote target...
Reading /home/paulp/ws/ws2/km/tests/hello_test.km from remote target...

Catchpoint 1 (exec'd /home/paulp/ws/ws2/km/tests/hello_test.km), 0x0000000000201032 in _start ()
(gdb)
```



Next set a breakpoint in hello_test, continue execution, the breakpoint fires, we look at local variables, and let the hello_test program finish:

```sh
(gdb) br printf
Breakpoint 2 at 0x201435: file musl/src/stdio/printf.c, line 5.
(gdb) c
Continuing.

Breakpoint 2, printf (fmt=0x20400d "%s %s\n") at musl/src/stdio/printf.c:5
5	{
(gdb) bt
#0  printf (fmt=0x20400d "%s %s\n") at musl/src/stdio/printf.c:5
#1  0x000000000020118e in main (argc=1, argv=0x7fffffdfdcf8) at hello_test.c:23
(gdb) f 1
#1  0x000000000020118e in main (argc=1, argv=0x7fffffdfdcf8) at hello_test.c:23
23	   printf("%s %s\n", msg, msg2);
(gdb) p msg
$1 = "Hello,"
(gdb) p msg2
$2 = 0x204007 "world"
(gdb) del br 2
(gdb) cont
Continuing.
[Inferior 1 (Remote target) exited normally]
(gdb)
```



Back to the output from the gdb_forker_test and hello_test programs, which shows:

- the hello_test output,
- km messages showing both instances of gdb detached,
- and the output from gdb_forker_test when the child process finishes

```sh
Hello, world
Hello, argv[0] = '/home/paulp/ws/ws2/km/tests/hello_test.km'
19:28:02.483754 km_gdb_detach        402  1001.km      gdb client disconnected
Child pid 1001 terminated with status 0 (0x0)
19:28:02.487561 km_gdb_detach        402     1.km      gdb client disconnected
[paulp@work tests]$
```


And, for completeness, the gdb_forker_test debug session finishes up:

```sh
[Inferior 1 (Remote target) exited normally]
(gdb)
```

## Appendix A - why did we choose bats for testing

A quick summary of testing framework requirements, choices and info on how we were choosing one

### Requirements

#### Test suite

* Uniform way to run tests and report results
  * ideally, test set is an executable (or script), uniformly named
* Support for individual tests and test suites
* Support for skipping or running individual tests or test groups (e.g. 'test only gdb-related stuff')
* no big environment expectations (i.e. we do not want Perl or C++ or autoconf/CMake just to build and run tests)
* Do not stop on an error but allow to skip related tests ()
* built-in check functions/macro (see http://testanything.org/)

#### Tests build

We need to have multiple ways of KM building for test. They mainly differ in compile flags and set of used libs, and name of produced executable (km_${TYPE}, e.g. km_obj km_prod km_debug) km->km_obj

* Regular build (aka obj) - what we use all the time. Optimization is on, --g is  on.
* debug build (aka debug)- no optimization. code coverage build
* Code coverage build (aka cov) - debug plus ` -fprofile-arcs -ftest-coverage`
* Production build (aka prod) - eventually. Asserts are skipped, optimization is turned on, no debug symbols

### Looked at and declined test frameworks

* [nUnit](https://nemequ.github.io/munit)
  * small/portable/easy. Tons of useful stuff like `munit_assert_memory_equal` :-)
  * support tests (functions) and suits (arrays)
  * has CLI and seems ready to run
  * **WOULD BE THE CHOICE** but unfortunately it messes with forks/signals and much more, and at this point (March '19) it's too much overhead to use it

* [Libtap](https://github.com/zorgnax/libtap)
  * simple set of macros for [TAP](http://testanything.org/), e.g. `ok(test(), "test descr"));`
  * just a C include file and one .c file
  * good for ONE test file. Real good for one file. Only
  * no suites or skips - and overall feels not worth spending time on.
* [CUnit](http://cunit.sourceforge.net/doc/index.html)
  * Looks like a good unit test env. but an overkill for now
  * Defines a bunch of helpful macros for asserts, pass/fail reporting (see [CUnit site](http://cunit.sourceforge.net/doc/writing_tests.html#tests))
  * Supports a live "test registry' where tests can be added programmatically
  * Supports test suits & groups (as array or description / functions)
  * supports output redirection/logs
  * feels unnecessary large and complex, requires "registry" for tracking tests, API to add to suites, etc.
* [check](https://libcheck.github.io/check/) or `dnf install check`
  * another unit testing, similar to the rest
  * expects autoconf, libtool, pkg-config cmake, and does not seem to add much compared to, say, nUnit - thus we will pass.
* [Criterion](https://github.com/Snaipe/Criterion)
  * Good:
    * Real nice framework, in active state, handles many aspects (e.g. signal testing, reports,  'theories' in TDD, etc...)
    * seems in active state, many forks
    * Their motto is `A cross-platform C and C++ unit testing framework for the 21th century`
    * Docs are [here](https://criterion.readthedocs.io/en/master/)
  * Not so good:
    * Dependencies on much stuff, total 15M (uses klib, debugbreak, nanopb - protobuffers)
    * requires build with cmake/gcc and install, set lib
  * Conclusion
    * **overkill for us**, skipping (for now), and not needed until we have a few full time test devs at least
    * we can always add it if needed as another step in BATS runs, skipping for now

BTW, `dependencies it uses need to be kept in mind` if we need good trees/streaming/json/etc, or protobufs, or debugreak in the code.

* http://attractivechaos.github.io/klib/#About
* https://github.com/scottt/debugbreak/
* https://jpa.kapsi.fi/nanopb/
