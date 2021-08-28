# Faktory

Faktory converts a docker container to kontain kontainer. For reference,
`container` will refer to docker container and `kontainer` with a `k` will
refer to kontain kontainer.

## Conversion

The faktory tool will convert a container to a kontain kontainer. A container
uses overlayfs to merge all layers of containers together. Some of these
layers contain the base OS, some contain the language runtimes + installed
libraries such as nodejs or python, and lastly some layers contain the
application payloads. The mechanism of the conversion will be to remove the
OS and language runtime layer, and replace these layers with kontain
generated base images.

Faktory will glue all the layers together using `overlayfs` to merge into a
final rootfs for the new image. Then we use `docker import` to import the
rootfs as a new image, squashing all the layers into a single rootfs layer.
However, the new image will not have any metadata. These metadata will need
to be restored.

Docker assumes the metadata of the image will not be changed unless a new
image is created. There is also no easy way to edit the metadata stored in
docker directly as the content of the metadata is hashed to become ID (digest
sha256) and a number of places internally docker checks if the hash match.

To work around the issue, we `docker save` the image, restore the metadata
from the source container, compute the hash and make the right edits, and
last `docker load` the resulting image back as the final `kontainer`. The
image exported through `docker save` is self contained, so we have a chance
to edit the metadata without failing all the docker hash checks. It's
important to note that this is reverse engineered process, and are not
officially supported APIs.

### Assumptions

A container can contain any number of things in the base image. It's
impossible to identify all possible different combinations of which layer is
the right layer to replace, without imposing at least some restrictions on
how the input container is constructed. Therefore, we assume the input
container is build with the following layers.

* Base OS layers: this layer contains the base operating system such as ubuntu
or alpine. We will throw this layout.

* Language runtime layers: this will contain the python installation. We can
identify this layer by searching for python installations in the known path.
We will ignore custom installation location for now. We will try to replace
this layer with kontain python runenv images.

* Installed libraries: we will keep this layer and will try to reconcile as
much as possible so the dependencies are preserved.

* Application layers: we will try to keep this layer untouched as much as
possible.

### Optimizations

In the future, we can replace .so files with the zero length tombstones. We
can also statically compile a new python.km with a list of .so files.

## Test

We implemented two simple integration tests under `tests`. The first one will
use `python` image which will be the same image that used in the test. The
goal will be to get the exact same image after the conversion. The second test
will use `kontainapp/runenv-python` image as the base.

### Prerequisite

The test requires:

- km be compiled and installed onto `/opt/kontain/bin/km` on the host.
- `kontainapp/runenv-python` is built. `make -C TOP/payloads/python
runenv-image`.
