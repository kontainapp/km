# Drop in KM payload

We want KM and related payload to be drop-in replacement for existing payloads, e.g python or java.

Currently (as of 5/5/2020) we support some cases by placing a shebang file instead of the original binary, and automatically adding '.km' to the payload name.
This way if there is a shebang file named 'java' (with `#!km` in it) and we have java.km in the same dir, the shebang works.

There is a set of reasons we want to improve on that:

* It quickly becomes a management headache. - e.g. when there is a shebang (say `gunicorn`) pointing to shebang (say `python`) we'd need to create gunicorn.km and python.km and symlinks...
* Shebang line length is limited in some kernels (https://lwn.net/Articles/779997/), generating weird failures . Or requiring us to use shell and thus have all related files in Kontainer
* Shebang does not accept multiple params, so we need to rely on /bin/env which is also different in different distros (e.g. alpine/busybox version does not support `-S`)
* It's generally confusing and requires thinking each time

So we want to better handle shebangs with KM.

## KM Shebang support use cases

I will use python as an example - the same text applies to all other interpreters or just binaries.

So there is how python binaries look like in /usr/bin :

```bash
lrwxrwxrwx. 1 root root     9 Mar 13 02:52 /usr/bin/python -> ./python3
lrwxrwxrwx. 1 root root     9 Mar 13 03:27 /usr/bin/python3 -> python3.7
-rwxr-xr-x. 2 root root 15408 Mar 13 03:28 /usr/bin/python3.7
```

And multiple python tools use shebang for their own invocation, e.g `/usr/local/bin/gunicorn`:

```bash
#!/usr/bin/python
# -*- coding: utf-8 -*-
import re
import sys
from gunicorn.app.wsgiapp import run

print(sys.argv)
if __name__ == '__main__':
    sys.argv[0] = re.sub(r'(-script\.pyw|\.exe)?$', '', sys.argv[0])
    print(sys.argv[0])
    sys.exit(run())
```

Use cases:
1. In containers, an entry point could be python (e.g *["python", "my script"]*) or a shebang (e.g. *["gunicorn", "-bind", "0.0.0.:8000"]*)
2. Even if an entry point is not pointing to a shebang files. a python script may still do `exec("guninicorn")`

**We want all the above cases to be handled transparently, without our need to modify the files**

## Calling payload directly

We can simply replace `python` with a shell script and have things like `python file.py` or `gunincorn --bind` to keep working, and avoid the issues above. e.g. `python` script can be something like this:

```bash
#!/bin/bash -e
/opt/kontain/bin/km --copyenv ${PYTHON_KM_DIR}/python.km  "$@"
```

However it relies on a shell - meaning we'd need to keep it in Kontainer even if it's not needed for entrypoint.

To avoid that, we will replace python with a link to KM, and deduct payload name (python.km) in KM from argv[0].

Specifically:
* remove the original python binary (in the example above, python.3.7) and replace it with a symlink to `/opt/kontain/bin/km`
* place python3.7.km (and the related symlinks python3.km and python.km) in the same directory
* Modify KM to look into argv[0] and `if basename(argv[0]]) != 'km'`, construct  payload name as `argv[0].km`, and then invoke this payload  (e.g. python.km), passing all args to it. No KM-specific args will be passed

Since that does not allow passing KM-speficic flags, we will also do the following:
  * Make *--copyenv* default. Adding '--copyenv' will be a noop until we clean up payload/faktory scripts using it, we can then drop the flag
    * --putenv flag will auto-cancel --copyenv. E.g. to avoid copy env, a fake '--putenv=FOO=BAR` can be used
  * env `KM_VERBOSE` can be set for tracing only. It is the "regexp" part of `-V<regexp>` flag
  * [separate PR]
   * we will allow passing flags to KM via environment KM_CLI_FLAGS, e.g. `'export KM_CLI_FLAGS="-Vfile --core-on-err"'`. It will be parsed BEFORE analyzing argv[0]
   * some flags (e.g. --restore) will be not supported with argv[0]-based payload name

## Calling payload via shebang

Let's say a python script executes `exec() `on a file whichn is a shebang file (e.g. `gunicorn`). *Exec* is handled by KM, so we'd need the KM to understand it's a shebang file, find out the actual payload file name,  form the proper args list and pass to the payload.

E.g. In case like `exec("gunicorn")`, KM will need to:
* find out `guninicorn` is a shebang, parse it
* form payload name (`python.km`)
* form arg list to python (add the arg from #! line)
* invoke payload `python.km` parse gunicorn file, find we will let linux kernel to parse the shebang and call KM instead of payload since KM

## Extension (.km) handling

Currently (5/5/20) we add .km to payload name to enable shebang to work (i.e. we place java.km near java shebang, Linux will call KM with 'java' payload name and we will add '.km' automatically).

With the above approach it is not needed, so Km will not do no manipulation with passed payload name, other than the argv[0]-based approach described abve.

This will also help in cases when there is a fork to native executable - e.g. gunicorn calls `/bin/uname`. Currently KM will try to load uname.km and it fails.
When we drop extension adding, it should simply work for native alpine binaries, and for .km file -  but we need to test.

Note that this will require changing Nokia faktory and other places which current generate shebang files and are dependent on this


## Snapshots

For now, resume from snapshots with FAIL if pointed to shebang file.

John writes *I think what here is reasonable, but we are going to think through how it pertains to snapshots. I think something with a long startup time like java is exactly the kind of thing we'll want to support with snapshot resume (assuming snapshot resume time is significantly faster than the initial process start time).*

I agree, and I think we should put something in the elf file so we know it's a snapshot and then just resume. This way we can snapshot java, put in java.km and place in container, and the rest is supposed to work .


## On symlinks to KM

 Usually shebang files (e.g. gunicorn) are parsed by the kernel,
but when shebang is passed to KM as a payload (e.g. on execve("gunincorn")), KM parses the shebang and extracts the payload name from there.
However, the payload name may point to something executable by KM (e.g. GO static binary) , or to a symlink to KM (e.g. "node").
We do not want KM to load elf of KM itself, so KM now does the same operation as it does with payload name - for payloads which are a symlink to KM,
the original payload name + .km is used

*** end of document ***