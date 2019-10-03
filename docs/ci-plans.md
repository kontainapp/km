# Plans for cleaning up make/containers/etc

This doc sketches the workflow, use cases, make targets for local dev and CI.

## KM and runtime

Dirs impacted: all dirs for core engine (monitor, runtime) build and test.

We support local `make` and `make withdocker`. For local make, no docker images are expected. For `make withdocker`, images need to be built or pulled

`make` and `make test` are used for full KM and runtime local build, and running KM tests locally, correspondingly.
For building on the local box without installing pre-requisites, use `make withdocker` which used docker container to build in local tree; or `make withdocker TARGET=test` which uses the docker container to run tests in local tree.

### When using `make withdocker`, buildenv once (docker build or pull)

Buildenv container needs to be built once, or pulled into local docker cache.

* `make buildenv-image` - creates buildenv image (whole 9 yard  - includes libstdc++ build)
* `make push-buildenv-image` - rare op, updates buildenv image
* `make pull-buildenv-image` - pulls the image from registry and re-tags it for local use. We do this to avoid image name dependency on the specific registry it is pulled from.

### Testing in docker container, locally or remotely

* `make test-image` - image with test environment - i.e. stuff needed for testing - bats code, /bin/time, timeout, etc.. (note: currently is FROM buildenv, can be scaled down if needed - since it's the only one pushed to Kubernetes for testing)
  * Note: currently kind of exist in `make test-image`
* `make run-test-image` - simple wrapper for local `docker run --device --u -ulimit` on the test image.
* `make push-test-image` - pushes test image to registry

Test image name includes fedora (or other linux distros when we officially support them), and tag is 'latest' by default (e.g. `kontain/test-km-fedora:latest`), but can be replaced for convenience - e.g. CI need to use BuildId there to avoid conflicts. Also, when doing some local testing with Registry pushes, ID a dedicated tag should be used to avoid conflicts - i.e. CI can invoke something like `make test-image IMAGE_TAG=CI-$(BuildId)`.

### Image tags on push and pull

All images are named `kontain/name-platform:version`.  Version default is `latest`, e.g. `kontain/buildenv-km-fedora:latest`.
Name may include payload (e.g. `node`) - e.g `kontain/buildenv-km-fedora` or `kontain/buildenv-node-fedora` or `kontain/test-km-fedora:latest` (for testing KM core)

**Push operations tags an image to target registry, push and then clear the tag**. e.g. tag `kontain/image:latest` as `kontainkubecr.azurecr.io/image:latest`
Pull does the opposite (pulls, then re-tags to `kontain/`)

## Payloads/*

Dirs impacted: all dirs for .km payloads (other than tests above) - generally under km/payloads.

km/payloads traverses down to individual payloads, so this is how 'make' behaves from top level:

Payload can be built from source (i.e. `git clone` first) or from pre-created docker images with build artifacts (i.e.`.a`) , aka so-called 'blanks'.

* `make all` builds all payloads.
  * `make all` started from KM repo top **will build all payloads**
  * By default, *make* will do build from blanks (will bring in blanks, build .km inside of the container, copy the results into the tree).
  * `make clobber` removes *payloads/payload_name/payload_dir* (`clobber` is payloads/ only target, it's not avail on top of the repo)
  * after `make clobber` or the first time, running `make fromsrc` does `git clone` `configure` and `build`. After that make all keeps re-building from source until `make clobber`
  * `make clean` run 'make clean' for payload source (if available) and removes .km and other link artifacts.
* `make test` runs all KM tests and **minimal tests subset** for payloads (aka sanity tests)
* `make test-all` runs all KM tests and payload tests

Each payload also supports the following:

* `make buildenv-image` - builds the blank (named *buildenv-payloadname-linuxdistro*), e.g. `buildenv-node-fedora`)
* `make pull-buildenv-image` - pulls images from (azure) registry, re-tags it for local runs (It does login first , or suggests to do login if fails).
* `make push-buildenv-image` - re-tags and pushes to registry (very rare, mainly when advancing minor version of payload, e.g. `python 3.7.4->3.7.8` )
* `make test-image` - builds image with testenv, km files and  and actual tests.
* `make run-test-image` - simple wrapper for local `docker run --device --u -ulimit` on the test image, mainly for local debugging
* `make push-test-image` - re-tags and pushes test image to registry, mainly for CI

`make distro` for now stays as is, and generates Kontainer with runnable payloads and some apps, mainly for demos.

## CI/CD

* CI/CD yaml files should use the above `make` targets, instead of direct manipulation with code or scripts
* CI/CD should `clean up images in registry` after the run ! (e.g. `make -C cloud/azure image-cleanup IMAGE_TAG=CI-$(BuildID)`  should go over all images and clean up the ones with this tag)
* `one time` - we will clean up everything that is not `latest` in the repo remove accumulated dirt

## Files layout

Each of the payload/name has .dockerignore and `docker` subdir. The subdir has a dockerfile for building the *blank* image and another for building test image. The blank is built in `docker` dir as it does not need anything from the source tree. The test is built in `payloads/name` as it needs bunch of files from payload build. *.dockerignore* eliminates unneeded files from being copied to dockerd on build.

For the monitor - same thing. We have `$TOP/docker` subdir 2 files (buildenv and test-image). Dockerfiles may be linux distro dependent, e.g. `buildenv-fedora.dockerfile`. We build buildenv in this dir, and we build test in `$TOP`. Top has .dockerignore for this build.

## Additional Makefiles changes

* All targets are in make/images.mk (fka `distro.mk`). kontain/km images should be GONE.Instead we explicitly copy `km` to test image dirs when for `docker build`.  This copy is temporary, until  we have a way to install KM+runk and use *runk* to executed Kontainers.

* Makefiles in payloads and tests include `images.mk` (we rename distro.mk to images.mk) only. KM include `actions.mk` , and ignores image-related targets.

* `make local-buildenv` convenient top level target (works on fedora only)  - installs dnf packages, logs in & pulls buildenv image, puts stdc++ lib in place. Not fancy, but prints help is something fails.

### Extra TODOs

* azure script cleanup, and use generic pull/push support from cloud/azure scripts

=== END OF THE DOC ===
