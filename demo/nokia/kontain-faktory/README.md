# Conversion to Kontain

Automation to convert Nokia java containers to the ones running the payloads in Kontain VM.

Phase 1 is adding KM and java.kmd/libc/so to regular docker images and we will run them as Docker containers.

All driven via make. `make help` for help

Typical sequence of commands:

```bash
make base # One time, for pulling and tagging orginal images. Assumes login.
make bas-retag # if you already pulled the containers, handy target to prep them for 'make all'
make all  # same as 'make' - builds kontain/nokia-* images from the base ones
make test # docker run for kontain kafka image - just sanity checking
```

To generate Kontain with extra KM flags, use `make clean all KMFLAGS="..."` (clean is needed since Makefile does not know the flags have changed).
For example, to get a test run with tracing VCPU and KM core dumps on payload errors:

```bash
make clean all test KMFLAGS="-Vcpu --core-on-err"
```

# TODO

* Adjust for properly running java from /opt/kontain/java/bin
* Old (and not needed) Java files can be stripped, and kontainers needs to be slimmed with docker-slim
