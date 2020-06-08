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

## Container use cases

I will use python as an example - the same text applies to all other interpreters or just binaries.

This is how python binaries look like in /usr/bin:

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

### Container entry point

In linux or with docker, a python program can be invoked using direct python invocation with script passed as an argument to python followed by script arguments:

```bash
python micro-srv.py --port=8080
```

or it could use shebang

```bash
./micro-srv.py --port=8080
```

Either of the above could be used as container entry point, either by the way of shell in between (shell form of CMD) or exec-ed directly (exec form of CMD).

### "Classic" km invocation

We used to invoke km by explicitly calling km with arguments:

```bash
path_to_km <km-options> payload <payload-options>
```

payload being either ELF .km file or shebang.
It is desirable to keep this functionality for convenience.


### Subprocess created by container payload

Container payload could create a new program, either via fork/exec or direct exec.
In addition the exec could be direct or via popen.
The new program could be invoked by referring to symlink, shebang, or python with arguments.

Unlike standard Linux environment all of the processing of these cases has to be performed by km.
The "_parent_" km has to find out if the exec is likely to succeed to report errors like exec would,
then it needs to prepare the arguments to the "_child_" km.
The "_child_" km would perform the same logic as the regular km invocation.

**We want all the above cases to be handled transparently, without our need to modify the payload files**

## Preparation steps to support symlinks

When preparing container image, in faktory:

* Remove the original python binary (in the example above, python.3.7) and replace it with a symlink to `/opt/kontain/bin/km`
* Place python3.7.km in the same directory.
  All other symlinks are intact, only the executable file gets replaced with symlink and .km file.
These steps make it so that either symlink or shebang entry point will execute km.

## Changes in KM

`argv[0]` is set by shell and is the program that was invoked on the command line.
More informative is AT_EXECFN auxv value, which reflects the file name parameter to `execve()` used to invoke the program.
It accounts for PATH traverse, and is either absolute, or relative to the current working directory path name.
Either way the chain of symlimks starting from `auxv[AT_EXECFN]` will bring us to km executable.
The last step on that path should point us to the .km file sitting next to the last symlink in the chain.
In case of python this will be `/usr/bin/python3.7` as symlink indicating `/usr/bin/python3.7.km` as payload.

With shebang `argv[0] == auxv[AT_EXECFN] == "/usr/bin/python"` as shebang requires absolute path.
If there is an argument on the shebang line then `argv[1]` is the argument and `argv[2]` is the shebang file name,
in the absence of the shebang line argument `argv[1]` is the file name.
Arguments on the shebang invocation would follow.

### Start

At the beginning, KM will look into `auxv[AT_EXECFN]`.
If `auxv[AT_EXECFN]` is a symlink, km will try to determine what payload is required.
Otherwise we assume "classic" invocation.

As we are in km we know the symlink ultimately resolves in km executable.
Because of the preparation steps above we know the last symlink in the chain should have the required .km next to it.

Starting with `auxv[AT_EXECFN]` iteratively follow symlink chain.
When we get to executable file (or simply not a symlink), the last step determines the payload name, which has to be next to the last symlink.

If the payload isn't found this way we assume this is "classic" invocation,
with km options and explicit payload name.
It will fail if the payload isn't specified or is incorrect.

No PATH traverse is needed.

These steps will take care of both symlink and shebang entry point invocation.

### Exec and popen

When guest program performs `execve()` or `popen()` km will do `exec()` itself, after forming an `argv[]` array.
Since there is no kernel or shell in the middle km will need to perform similar processing.

The only executable km will ever exec is itself, i.e. `argv[0]` for the "_child_" km is always km itself (`"/proc/self/exe"`).
Regardless of the specifics (`popen()`, shebang, or straight `execve()`), the target can only be symlink to km with payload sitting next to symlink -
anything else is treated as an incorrect executable.
Once we found that the search is done and we are ready to exec into "_child_" km.

First argument to guest `execve()` is the guests view on the executable file.
It is conceptually similar to `auxv[AT_EXECFN]`.
It could be absolute of relative path name.
Unlike km initial start it could be `"/bin/sh"` in case of `popen()`, shebang file, or symlink resolving to km.
Anything else is failure.

In case of `popen()` the executable file name is `"/bin/sh"`, "`argv[1] == "-c"`, `"argv[2]"` is requested command.
This is how we know to perform `popen()` specific processing, which includes guest PATH traverse.
The executable found here could be symlink or shebang, so processing falls through to the next step.

Otherwise executable file name maybe a symlink resolving to km or shebang, or it could be straight shebang.
Note that guest has already performed its PATH traverse, so the name is absolute or relative full name.

We iteratively follow symlinks until regular file.
That file is either km executable or shebang, anything else is failure.
In case of symlink the last step determines payload name.
In case of shebang the iterative symlink is performed again.
To look for shebang km checks if the first two characters of the target file are `"#!"`.
We parse the shebang line to extract the executable name and optional argument.

_Note that Linux supports nested shebangs. At this point we don't support that, failing instead_

## KM options

[separate PR, postpone for now]
* we will allow passing flags to KM via environment KM_CLI_FLAGS, e.g. `'export KM_CLI_FLAGS="-Vfile --core-on-err"'`.
  It will be parsed BEFORE analyzing argv[0]
* some flags (e.g. --restore) will be not supported with argv[0]-based payload name

## Extension (.km) handling

With this approach so Km will not do no manipulation with passed payload name, other than the argv[0]-based approach described above.

Note that this will require changing Nokia faktory and other places which current generate shebang files and are dependent on this

## Snapshots

For now, resume from snapshots with FAIL if pointed to shebang file.

John writes *I think what here is reasonable, but we are going to think through how it pertains to snapshots. I think something with a long startup time like java is exactly the kind of thing we'll want to support with snapshot resume (assuming snapshot resume time is significantly faster than the initial process start time).*

I agree, and I think we should put something in the elf file so we know it's a snapshot and then just resume. This way we can snapshot java, put in java.km and place in container, and the rest is supposed to work .

*** end of document ***