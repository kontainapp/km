# Testing

A quick summary of testing framework requirements, choices and info on how to use the chose one

## Requirements

### Test suite

* Uniform way to run tests and report results
  * ideally, test set is an executable (or script), uniformly named
* Support for individual tests and test suites
* Support for skipping or running individual tests or test groups (e.g. 'test only gdb-related stuff')
* no big environment expectations (i.e. we do not want Perl or C++ or autoconf/CMake just to build and run tests)
* Do not stop on an error but allow to skip related tests ()
* built-in check functions/macro (see http://testanything.org/)

### Tests build

We need to have multiple ways of KM building for test. They mainly differ in compile flags and set of used libs, and name of produced executable (km_${TYPE}, e.g. km_obj km_prod km_debug) km->km_obj

* Regular build (aka obj) - what we use all the time. Optimization is on, --g is  on.
* debug build (aka debug)- no optimization. code coverage build
* Code coverage build (aka cov) - debug plus ` -fprofile-arcs -ftest-coverage`
* Production build (aka prod) - eventually. Asserts are skipped, optimization is turned on, no debug symbols

### Code coverage

We will use gcov. More tbd

## Choices

* [Libtap](https://github.com/zorgnax/libtap)
  * simple set of macros for [TAP](http://testanything.org/), e.g. `ok(test(), "test descr"));`
  * just a C include file and one .c file
  * no suites or skips - nothing fancy.
  * good for ONE test file. Real good
* [BATS](https://github.com/bats-core/bats-core) (or `dnf install bats`)
  * TAP-compliant testing framework for Bash
  * allows to scan directories / runs multiple tests(named `<test>.bats`)
* [CUnit](http://cunit.sourceforge.net/doc/index.html)
  * Looks like a good unit test env. but an overkill for now
  * Defines a bunch of helpful marcos for asserts, pass/fail reporting (see [CUnit site](http://cunit.sourceforge.net/doc/writing_tests.html#tests))
  * Supports a live "test registry' where tests can be added programmatically
  * Supports test suits & groups (as array or description / functions)
  * supports output redirection/logs
* [nUnit](https://nemequ.github.io/munit)
  * small/portable/easy. Tons of useful stuff like `munit_assert_memory_equal` :-)
  * support tests (functions) and suits (arrays)
  * has CLI and seems ready to run
* [greatest](https://github.com/silentbicycle/greatest)
  * Another set of marcos / C  / CLI , similar to nUnit
  * Good review (and comparison with others) [here](https://spin.atomicobject.com/2013/07/31/greatest-c-testing-embedded/)

At this stage `BATS` and either `nunit` or `greatest` seem the best.

## Next steps

* Convert a couple of tests and decide
* Add to build
* Convert the rest
* look at coverage/build modes
