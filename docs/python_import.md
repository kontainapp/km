
# Notes on Python Import

Imports of pure python modules are handled by the `cpython` interpreter using simple `read(2)`. Imports of binary extensions use `dlopen(3)` to dynamically load new functionality. The binary extensions that are Python built-ins and standard libraries are included with in the `cpython` source and are built automatically built automatically.

Kontain's approach is static linking, so the `dlopen(3)` approach for extensions doesn't work for us. There isn't a problem for built-ins and standard library extensions because `cpython` knows how to statically link. This is a problem for add-on packages that are designed to create one or more shared libraries.

Our approach is to create application specific `cpython` builds that use the `Setup.local` file to statically link with any add-on extensions included with imported packages. The obvious problem with this approach is module names in `Setup.local` are limited to a single level (eg. 'xyz'). Add on extensions are typically part of a package with multi-level package names (eg. 'uvw.xyz'). A solution to this problem for Python 3 is:

* Mangle the extension module names in `Setup.local` to encode the multi-level name.
* Insert a `importlib.abc.MetaPathFinder` object at the beginning of `sys.meta_path`. The new object checks to see if mangling a name to be imported matches a name in `sys.builtin_module_names`. If there is a match then the built-in module is returned for import.

* Build static example ( run from `payloads/python`)
  * `git clone github.com:pallets/markupsafe.git` cpython/Modules/markupsafe
  * Add `_speedups markupsafe/src/markupsafe/_speedups.c` to `cpython/Modules/Setup.local 
  * Run `build.sh` to build python.km with `markupsafe` included.

# Algorithm: Include Add-On Extensions with CPython

Step 1: Built the module for standard python (shared objects).
* Inside the `cpython/Modules` directory
  * `git clone <git-path>
  * `cd <package>`
  * `python3 setup.py build`

Step 2: Extract new `Setup.local` lines:
* Inside `cpython/Modules`
  * `../../build_setup_local <dir>...` (Multiple directories allowed)
  * Add output to `Setup.local`
  

## Nokia Wish List

## flask https://github.com/pallets/flask

Light-weight HTTP services (compared to Django)

* (pure python)
* Installation Requirements:
  * Werkzeug https://github.com/pallets/werkzeug (pure python)
  * jinga2 https://github.com/pallets/jinja  (pure python)
    * requires markupsafe
  * itsdangerous https://github.com/pallets/itsdangerous (pure python)
  * click https://github.com/pallets/click (pure python)

## gevent https://github.com/gevent/gevent.git

Coroutine-based concurrency library for Python

* Uses cython. Generates 17 shared libraries.
* Installation Requirements
  * greenlet https://github.com/python-greenlet/greenlet  (uses extensions)
    * Interesting module performing stupid stack tricks.
  * cffi https://bitbucket.org/cffi/cffi/src/default/ (uses extensions)
    * Requires: pycparser https://github.com/eliben/pycparser (pure python)

## requests https://github.com/psf/requests

"HTTP for Humans" HTTP client side

* pure python
* Installation Requirements:
  * chardet https://github.com/chardet/chardet (pure python)
  * idna https://github.com/kjd/idna (pure python)
  * urllib3 https://github.com/urllib3/urllib3 (pure python)
  * certifi https://github.com/certifi/python-certifi (pure python)
  * extra: pyOpenSSL https://github.com/pyca/pyopenssl
  * extra: cryptography https://github.com/pyca/cryptography
  * extra: PySocks (Widows. don't care)

## pillow https://github.com/python-pillow/Pillow

The friendly PIL fork (Python Imaging Library)

Requires:

* Required linux libs: zlib and libjpeg required.
* Optional linux libs: libtiff, libfreetype, littlecms, libwebp, tcl/tk, openjpeg, libimagequant (GPL3), libraqm
* Uses extensions

## Keras
* keras https://github.com/keras-team/keras.git (pure python)
Requires (install_requires from `setup.py`):
  * keras_preprocessing https://github.com/keras-team/keras-preprocessing
  * keras_applications  https://github.com/keras-team/keras-applications
  * h5py https://github.com/h5py/h5py
  * pyyaml https://github.com/yaml/pyyaml
  * six https://github.com/benjaminp/six
  * scipy https://github.com/scipy/scipy

### numpy https://github.com/numpy/numpy


## tensorflow
* tensorflow https://github.com/tensorflow/tensorflow.git
* Built with `bazel`, not setuptools.

## How to map `.o` files to extension package
Assume: Run `python3 setup.py build_ext`
`readelf -s <.so file> | grep FILE` is a list of the source files that 