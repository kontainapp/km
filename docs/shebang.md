# Drop in KM payload

We want KM and related payload to be drop-in replacement for existing payloads, e.g python or java.

Currently (as of 5/5/2020) we support some cases by placing a shebang file instead of the original binary, and automatically adding '.km' to the payload name.
This way if there is a shebang file named 'java' (with `#!km` in it) and we have java.km in the same dir, the shebang work.

There is a set of reasons we want to improve on that:

* it quickly becomes a management headache. - e.g. when there is a shebang (say `gunicorn`) pointing to shebang (say `python`) we'd need to create gunicorn.km and python.km and symlinks...
* shebang line length is limited in some kernels (https://lwn.net/Articles/779997/) , generating weird failures . Or requiring us to use shell and thus have all related files in Kontainer
* shebang does not accept multiple params, so we need to rely on /bin/env which is also different in different distros (e.g. alpine/busybox version does not support `-S`)

So we want a simple shebang shebang support directly in KM - it will behave similarly to shell, or pythion, or the like and will read the shebang file for commands

## KM Shebang support

First of all, we can simply replace python with a shell and have things like gunincorn to keep working, and avoid the issues above. e.g. `python` script can be something like this:

```bash
#!/bin/bash -e
/opt/kontain/bin/km --copyenv --snapshot=/var/kontain/snapshots  ${YPYTHON_KM_DIR}/python.km  "$@"
```

However it relies on a shell - meaning we'd need to keep it in Kontainer even if it's not needed for entrypoint.
To avoid that (and to avoid relying on specific version of `env` or length of shebang line) we will support this format:

```bash
#!/opt/kontan/bin/km
flags: -Vfile --copyenv ---snapshot=/var/kontain/snapshots  -e MORE=MORE_VALUE
payload: /usr/local/bin/python.km
```

KM will simply read commands from the file starting with `#!`.

* In the future we may accept yaml there
* For now it will be simple `key: value`
* Only 2 keys are supported `flags` and `payload`.
* `#` in the beginning of a line is a comment

## extension handling

Currently (5/5/20) we add .km to payload name to enable shebang to work (i.e. we place java.km near java shebang , Linux will call KM with 'java' payload name and we will add '.km' automatically).

With the above approach it is not needed, so Km will do no manipulation with payload name.
This will also help in cases when there is a fork to native executable - e.g. gunicorn calls `/bin/uname`. Currently KM will try to load uname.km and it fails.
When we drop extension adding, it should simply work for native alpine binaries, and for .km file -  but we need to test.


### Attack surface note (no action)

The above actually means we will may still have the native binary in a Kontainer and it can be run withut KM (e.g. with `--entrypoint` for docker or `cmd:` for k8s, assuming malicious intent).
That kind of kills "sandboxing" for this case. I do not see a way around it without our own runtime (docker) and admission controller (k8s)

## Snapshots

For now, resume from snapshots with FAIL if pointed to shebang file. I am open to suggestions here.

## Fork

No impact - should just work (do we need a test?)

*** end of document ***