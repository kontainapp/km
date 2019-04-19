# Python as KM Payload

In this directory we build cpython (Python interpreter written in C, the default one) as a KM payload.

The process includes cloning python git repo, patching python config, and building python.km, using MUSL-based runtime.

We also support building a Docker container with pythin.km and pushing it to Azure Container Registry.

## Building python as a KM payload

`make` or `make buid` (which will run ./build.sh`).

* It will clone all the involved repos
* it will then build musl, km, python and python.km.

After it is done, you can pass `cpython/python.km` to KM as a payload, e.g. `../../build/km/km ./cpython/python.km scripts/hello_again.py`

## Building distro package and publishing it

`make distro` and `make publish` will build Docker image and publish it to Azure ACR. We expect that KM is already packaged as a container, and you logged in to ACR 9for publish. So generally, it's better to do `make distro` from top level - this will build KM image as well (or you can `make -C ../../km distro` to get KM image built)

## Known issues

* There are rough edges in locale management - you may see some error messages but all should work.
* if python code uses a syscall we have not implemented yet, it can exit with KM SHUTDOWN
* default modules are packages into the container and provided to python as host filesystem ; no mechanism for other modules or C-based modules, other than compile them in (at the moment)
* No real tests
* I am sure there is more, it is a prototype
