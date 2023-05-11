# How to run valgrind on km

## Dependencies and prerequisites

Need to install valgrind:

```
sudo dnf install valgrind.x86_64
```

Need to rebuild `km` as dynamic executable. The main reason for that is that default valgrind suppression rules to exclude noise from libc and such are written based on .so file names.
Edit `make/custom.mk` to remove `-static` and rebuild `km`.

## Running

For simple quick check run:

```
valgrind --sim-hints=lax-ioctls --trace-children=yes --suppressions=km_valgrind.supp ../build/km/km hello_test.km
```

This will print the report on the terminal. To redirect report to a file add `--log-file=/tmp/v.report`:

```
valgrind --sim-hints=lax-ioctls --trace-children=yes --suppressions=km_valgrind.supp --log-file=/tmp/v.report \
   ../build/km/km hello_test.km
```

If there are memory errors running with extra `--read-var-info=yes --track-origins=yes` will provide better reports at the expense of noticeably slower runs:

```
valgrind --sim-hints=lax-ioctls --trace-children=yes --suppressions=km_valgrind.supp --log-file=/tmp/v.report \
   --read-var-info=yes --track-origins=yes ../build/km/km hello_test.km
```

## Results

As of early Feb 2020, (commit 18e40972da2d4f69270bb05bec05a4a4ade27a85) there are no memory errors running our tests and `make test-all` in python.

There are three memory leaks that seem to be coming from `libc` implementation of `pthread_create`, they are excluded by the `--suppressions=km_valgrind.supp`.


## valgrind support has been added to some of our Makefile targets

You need to build km with minor changes to accomodate valgrind:
```
make -C km valgrind
```
Once that completes a valgrind version of km will be placed in build/km/valgrind/km and build/opt/kontain/valgrind/bin.

## Run bats tests with valgrind

Run this to have the bats tests run with km and valgrind:
```
make -C tests test-valgrind
```
