# Python as KM Payload

In this directory we build cpython (Python interpreter written in C, the default one) as a KM payload.

The process includes cloning python git repo, patching python config, and building python.km, using MUSL-based runtime.

We also support building a Docker container with pythin.km and pushing it to Azure Container Registry.

## Building python as a KM payload

`make` builds using 'blank' conainer (use `make buildenv-image` or `make pull-buildenv-image` to prepare environment)
`make clobber; make fromsrc` will do local make:

* It will clone all the involved repos
* it will then build musl, km, python and python.km.

After it is done, you can pass `cpython/python.km` to KM as a payload, e.g. `../../build/km/km ./cpython/python.km scripts/hello_again.py`

## Building distro package and publishing it

`make runenv-image` and `make push-runenv-image` will build Docker image and publish it to Azure ACR. 

## Known issues

* if python code uses a syscall we have not implemented yet, it can exit with KM SHUTDOWN
* I am sure there are more, so this list is a placeholder
