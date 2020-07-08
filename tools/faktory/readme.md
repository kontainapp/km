Faktory converts a docker container to kontain kontainer. For reference,
`container` will refer to docker container and `kontainer` with a `k` will
refer to kontain kontainer.

# Conversion

The faktory tool will convert a container to a kontain kontainer. A container
uses overlayfs to merge all layers of containers together. Some of these
layers contain the base OS, some contain the language runtimes + installed
libraries such as nodejs or python, and lastly some layers contain the
application payloads. The mechanism of the conversion will be to remove the OS
and language runtime layer, and replace these layers with kontain generated
base images.

For now, after identifying all the layers, we will glue all the layers
together using `overlayfs` to merge into a final rootfs for the new image.

## Assumptions

A container can contain any number of things in the base image. It's near
impossible to identify all possible different combinations of which layer is
the right layer to replace, without imposing at least some restrictions on
how the input container is constructed. Therefore, we assume the input container
is build with the following layers.

* Base OS layer: this layer contains the base operating system such as ubuntu
and alpine. We will throw this layout.

* Launguage runtime layer: this will contain the python installation. We can
identify this layer by searching for python installations in the known path.
We will ignore custom installation location for now. We will try to replace
this layer with kontain python runenv images.

* Installed libraries: we will keep this layer and will try to reconcile as
much as possible so the dependencies are preserved.

* Application layer: we will try to keep this layer untouched as much as
possible.

## Optimizations

In the future, we can replace .so files with the zero length tombstones. We can also statically compile
a new python.km with a list of .so files.