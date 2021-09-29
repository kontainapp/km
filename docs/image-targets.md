# Image-related make targets

**THIS IS NOT A BUILD GUIDANCE DOCUMENT** - `docs/build.md` is the building guidance, and `docs/azure_pipeline.md` is the doc with hints and FAQ about using CI and running stuff there.

This doc sketches the workflow, use cases, make targets for local dev and CI, and while we try to keep it reasonably accurate, this is more of a design doc

The design goals are as follows:

* have all docker image based functionality in makefiles so they can be invoked and used locally and in CI
* avoid make/CI code drift
* Support fast build of payloads,as well as necessary libs (e.g. libstdc++)

## Targets for local build and test of KM and runtime

We support local `make` for build in local source tree using host environment (assumes pre-requisites installed), and `make withdocker` for build in local tree using docker image.
`make test` and `make withdocker TARGET=test` do testing of the local built executables, the former in the host environment, and the latter using the same docker image with build environment

Docker images for build environment need to be built or pulled with `buildenv-image` or `pull-buildenv-image` target.

* Note that **a set of pre-requisites need to be installed on the host in order for local `make` to work**.
  * `tests/buildenv-fedora.dockerfile` has a list of dnf packages to install; another pre-requisites is kontain libstdc++
  * a simple way to get all in is to run `make -C tests buildenv-local-fedora` AFTER pulling ot building buildenv image

### Docker image for build environment

Dockerized build is using `buildenv-component-platform` images (i.e.`buildenv-km-fedora` or `buildenv-node-fedora`) which need to be either built once, or pulled into local docker cache once. Also, the same image is used when pre-requisites are being installed locally with `make -C tests buildenv-local-fedora`.

* `make buildenv-image` - creates buildenv image (whole 9 yard  - includes libstdc++ build).**This is a long process as it installs many packages, clones gcc and builds libstdc++**
* `make pull-buildenv-image` - pulls the image from (Azure) container registry and re-tags it for local use. We do this to avoid image name dependency on the specific registry it is pulled from.
  * Before pull from Azure, you need to install *az command line* (https://docs.microsoft.com/en-us/cli/azure/install-azure-cli-yum?view=azure-cli-latest) and run `make -C cloud/azure login`
* `make push-buildenv-image` - rare op, updates buildenv image
* `make -C tests buildenv-local-fedora` is a convenience target which extracts prerequisites (DNF package list and libstdc++ lib) from buildenv image and installs on on local box, thus allowing to run regular `make`

#### Build environment on dockerhub

For some usecases, we want to access `buildenv` images through dockerhub, instead of our
private container registry on azure. To pull from dockerhub:
```bash
CLOUD=dockerhub make pull-buildenv-image
```

In the same way, to push the `buildenv` image to dockerhub:
```bash
# Optional: login into dockerhub
make -C cloud/dockerhub login

# Push to dockerhub
CLOUD=dockerhub FORCE_BUILDENV_PUSH=force make push-buildenv-image
```

### Image Versions

We use the following environment (and Makefile) variables to control which versions are used:

* `IMAGE_VERSION` - use this version (aka Docker tag) for all container images. Default is `latest`
* `BUILDENV_IMAGE_VERSION` - use this version (aka Docker tag) for all buildenv images. Default is `IMAGE_VERSION`

For example, if IMAGE_VERSION is set to `myver`, then default for all buildenv images will also be `myver`. If you want to use latest buildenv images with `myver` of test images, set BUILDENV_IMAGE_VERSION to *latest*

### Testing in docker container, locally or remotely

* `make testenv-image` - image with test environment - i.e. stuff needed for testing - bats code, /bin/time, timeout, etc.. (note: currently is *FROM buildenv*, can be scaled down if needed - since it's the only one pushed to Kubernetes for testing)
* `make push-testenv-image` - pushes test image to registry. Since all test images are different, **we require to pass IMAGE_VERSION=version** for this target to work. **Version* can be SHA. or buildID, or other unique tag to diffirentiate images - e.g. CI uses `make testenv-image IMAGE_VERSION=ci-$(BuildId)`. We will block *:latest* tag to avoid accidental conflicts.

To run full test in Docker container (based on testenv-image) we provide 2 simple wrappers:

* `make test-withdocker` - runs 'test' target rules in docker using testenv-image.
* `make test-all-withdocker` - runs 'test-all' (extended) target rules in docker using testenv-image.

### Full image names

All images are named `kontain/name-component-platform:version`.

* Version default  is `latest` for buildenv images e.g. `kontain/buildenv-km-fedora:latest`.
* Names are `buildenv` for build environment and `test` for test images - e.g `kontain/buildenv-km-fedora` or `kontain/buildenv-node-fedora` or `kontain/test-km-fedora:ci-512`
* **Push operations adds Docker image name alias (aka tag) pointing to to target registry, pushes and then clear the tag**. e.g. image `kontain/image:latest` will be tagged as `kontainkubecr.azurecr.io/image:latest` before push.
* Pull does the opposite (pulls, then re-tags to `kontain/`)

## Payloads

Dirs impacted: all dirs for .km payloads (other than tests above) - generally under km/payloads.

km/payloads traverses down to individual payloads, so this is how *make* behaves from top level:

Payload can be built from source (i.e. `git clone` first) or from pre-created docker images with build artifacts (i.e.`.a` libs). We refer to these images as `blanks`.

* `make all` builds all payloads.
  * `make all` started from KM repo top **will build all payloads**
  * **By default, we build from blanks** - i.e. will bring in blanks, build .km inside of the container, and copy the results into the source tree.
  * `make clobber` removes *payloads/payload_name/payload_dir* (`clobber` is payloads/ only target, it's not avail on top of the repo)
  * `make clean` run 'make clean' for payload source (if available) and removes .km and other link artifacts.
* `make fromsrc` - when running the first time, or after `make clobber` this fetches sources from remote git and does full build (i.e. `git clone` + `configure` + `build`).
  * After that `make all` keeps re-building from source until `make clobber`
* `make test` runs all KM tests and **minimal tests subset** for payloads (aka sanity tests)
* `make test-all` runs all KM tests and payload tests
* `make test-withdocker` - a wrapper running `make test` rules in `testenv-image` container
* `make test-all-withdocker` a wrapper running `make test-all` rules in `testenv-image` container

Each payload also supports the same targets supported for KM, but uses payload-specific *component*, e.g. *node*:

* `make buildenv-image` - builds the blank (named *buildenv-component-platform*), e.g. `buildenv-node-fedora`)
* `make pull-buildenv-image` - pulls images from (azure) registry, re-tags it for local runs (it expects that loginto Docker registry - was already done. See `make -C cloud/azure login`)
* `make push-buildenv-image` - re-tags and pushes to registry (very rare, mainly when advancing minor version of payload, e.g. `python 3.7.4->3.7.8` )
* `make testenv-image` - builds image with testenv, km files and actual tests.
* `make run-testenv-image` - simple wrapper for local `docker run --device --u -ulimit` on the test image, mainly for local debugging
* `make push-testenv-image` - re-tags and pushes test image to registry, mainly for CI
* `make runenv-image` - builds bare minimum image to be released.
* `make push-runenv-image` - retag and pushes runenv image to registry.
* `make demo-runenv-image` - builds an application demo using the runenv-images. Used for python and node.
* `make push-demo-runenv-image` - retag and pushes runenv demo image to registry.

## CI/CD

* CI/CD yaml files  use the above `make` targets, instead of direct manipulation with code or scripts

### Running a CI with custom buildenv images

If a PRs needs changes to buildenv images, it needs to have these new buildenv images built, pushed to Azure
and then used during CI run for this PR, all with as little impact to other runs as possible.

Also, the PR may need to change ALL buildenv images if they are impacted by the base one.

If the new buildenv image is just an extension of the existing one (and backward compatible), do the following:

1. Make sure the image works by using PR with the custom image and BUILDENV_IMAGE_VERSION set (below)
2. Make a small PR with just the image change. When approved, merge, and push the new image as :latest to Azure
3. Drop custom image details from your original PR and merge when approved/tested

If new buildenv image is NOT backward compatible and will break prior builds. Do the following:

1. Make sure the image works by using PR with the custom image and BUILDENV_IMAGE_VERSION set (below)
2. Merge it as is. This way anyone who rebases will still have working build
3. Ask everyone to pause (not submitting and not looking at PRs)
4. Make a small PR which rollbacks custom imagename. Push new image as :latest, merge the PR
5. Let everyone know to rebase and resume


#### Testing custom image using BUILDENV_IMAGE_VERSION variable

To support this use case BUILDENV_IMAGE_VERSION env variable is used in makefiles.
If set, it will change the version (aka tag) of all buildenv images used in a specific makefile. It will impact all dockerfiles which will be doing `FROM whatever-image:$BUILDENV_IMAGE_VERSION` and all `docker build` which will tag new version with this tag.

Also the pipeline (`azure-pipeline.yml`) defines this one as `buildenv_image_version: latest # use this for all buildenv containers`.

So if generic changes to *all* buildenv images is needed, the env variable can be set in the shell for the duration of work, and the CI yml file in a PR can be changed to point to the new value. The **new value** is anything unique, usually the name of the branch without special characters

If the change is needed for specific buildenv images only, then the variable needs to be passed individually to the needed Makefiles. E.g. if the buildenv image for Java has changed, these are the steps to build and push:

```sh
tag=myTag  # something unique e.g. branch name with no special char`
cd payloads/java
make buildenv-image
eval $(make print-BUILDENV_IMG)  # this will place the image name (sans tag) into $BUILDENV_IMG
docker tag $BUILDENV_IMG:latest $BUILDENV_IMG:$tag
make push-buildenv-image BUILDENV_IMAGE_VERSION=$tag
```

Then, the steps in the CI that use this buildenv image need to get the proper version passed, e.g. building of Java testenv:

```sh
make testenv-image  BUILDENV_IMAGE_VERSION=myTag
```

Then all the CI tests will use the test environment built from correct image

To summarize, the steps to test with a custome buildenv image are as follows:

* build the image
* tag it with a unique tag
* push to azure with this tag
* change azure yamls (in your PR only) to use this tag by passing BUILDENV_IMAGE_VERSION to the steps that need the custom image. E.g. add `BULDENV_IMAGE_VERSION=mytag` to a yaml line like this one:
`make -C cloud/azure ci-prepare-testenv LOCATION=payloads/java prepare_testenv`


## Files layout

* Dirs where buildenv and test images are build are defined by `BUILDENV_PATH` and `TESTENV_PATH` make vars.
* Each dir has *.dockerignore* to eliminate unneeded files from being copied to `dockerd` on build.
* See `images.mk` for details

## Additional Makefiles changes

* All targets for manipulating buildenv and test images are in `make/images.mk`. Note that this copy is temporary, until  we have a way to install KM+runk (runk: Kontain runtime for docker and k8s, if we choose to ship our own) and use *runk* to executed Kontainers.

* Makefiles in payloads and tests include `images.mk` only. KM and runtime include `actions.mk`, and ignores image-related targets.

### Extra TODOs

* We will do periodic clean of old version in the repo, to remove accumulated dirt. (https://github.com/kontainapp/km/issues/926)
  * we can use ACR tasks for it, remove all images older than 3 days; or just piggyback on CI run clean up if it's not there already

=== END OF THE DOC ===
