# KM releases

## What's a release

A release is Github collection of sources (auto-packaged by github) and extra files (manually or script-added to the "release files) hosted on github.

We create releases in public kontainapp/km-release repo so KM sources are not released but KM binaries can be picked up for evaluation.

A release may include multiple `.tar.gz` file, but there is at least one - `kontain.tar.gz` with the KM binaries, libs and tools, which is created, and is expected to be installed before other tars. All files related to release are expected to be on github, with the exception of Docker images which can be on dockerhub.

Release content is described in `docs/planning/*.md`.

* We maintain releases on [github km-releases](https://github.com/kontainapp/km-releases/releases/).
* We also maintain install script and basic install documentation there.
* This repo is a submodule to KM and is sitting in `./km-releases` (after 'git submodule sync --init).
* Everything else (including release documentation) is kept in [github km](https://github.com/kontainapp/km).
* Note: `km-release` is public repo, so no authentication is needed for read access.

If a payload need to put extra files im a release, it needs to implement `release:` make target to and package extra files. The format, location and process is up to a payload but install process needs to be documented in km-releases/README.md.

## Naming convention for the releases

* `v0.1-test` if release name is not defined or defined without 'v' in front. we will fallback to default release tag `v0.1-test`.
  * When a release with this tag is created, it **automatically overrides the prior one with the same tag**.
  * This allows easier pipeline testing
* `v0.1-beta` for this release name, we also automatically override the existing release
  * This allows us to manually replace beta release as we update the code base.
  * at some point we will start using unique names (e.g. `0.1-beta-rc1`) but for now I do not want to pollute the list of releases on github
* `For any other tag`, if a release with this tag already exists on km-releases repo, publishing of a release will fail.

Also:

* `any tag starting with v` pushed to KM repo will trigger release pipeline.
* km-releases installation script looks into `km-releases/default-release` to pick up release name to be installed by default.

We recommend using unique tags, e.g. `v0.1-beta-demo-nokia` for manual uploads of anything we demo or give to people to try.

## How km-releases installation script picks up the release to install

km-releases installation script installs a release passed a the first argument. If the arg is empty, it extracts the release tag from `default-release` file expected to be present in the root dir for km-releases repo master brach. See `km-releases` repo for more info
## How to create a release

You can create release by manually running `make release` or triggering CI release pipeline. Details below

### Manual steps to create a release

Assuming the product build has succeeded, here are the steps to build a release

Prerequisites

1. Create a github personal token and place it's value in GITHUB_RELEASE_TOKEN env var. (https://github.com/settings/tokens)
1. Make sure PyGitHub python module is installed `pip3 install --user PyGitGHub`

Steps

1. `make release runenv-image` to build release tarball(s) and payload run environment images
1. Publish the release on github using `RELEASE_TAG=<your_tag> make -C km publish-release`. Default tag is `v0.1-test`
1. Publish runenv images to dockerhub with `make publish-runenv-image`
1. Validate the release by following install instruction on https://github.com/kontainapp/km-releases. Note: on CI the release is auto-validated.

### CI automation to create a release

We use existing Azure pipeline mechanism to generate releases.

A release CI pipeline is triggered by creating (and pushing) `v*` tag in KM, or pushing to a branch named `releases/*/*` (e.g. /releases/msterin/v1.0-try`).
The latter will create a release named `v0.1-test`.

For example, this will trigger the CI release pipeline and create a unique (and not overridable) `v0.1-test-manual` release:

```bash
tag=v0.1-test-manual; git tag $tag; git push origin $tag
```

We also auto-trigger the same release pipeline for any branch named 'releases/*/*, e.g. `releases/beta/snapshot-api`
The release pipeline builds, publishes and validates the release. See steps in `azure-pipeline-release.yml`

### Use case and internal process

* We decide a specific commit in master or release branch is good enough for release. The commit should be tested by existing pipelines already
* We decide to name this release, say `v0.9-beta`
* We create a tag `v0.9-beta` on the commit and push the tag
  * when/if we need a branch to work on patches, we will create `releases/v0.9-beta` and will be tagging it separately for sub-releases, e.g. `v0.9.1-beta`
  * Note, in git, a tag is independent of branch and associated only with a commit. Therefore, when a tag is pushed, the pipeline will be triggered.
* This trigger a new Azure pipeline on version `v*` tags to generate and push the artifacts
  * Thus pipeline uses standard pipeline components to build the product
  * It invokes `make release` to generate release artifacts
  * It invokes `make publish-release` to call a (new) script using github API
    * The script uses the current branch name to identify release tag
    * It creates a release (say `v0.9-beta`) and pushes the artifacts there.
    * `NOTE` this requires a github Personal Access Token (PAT).
    * `NOTE2` if a release already exists, the release script will fail. If the intent is to replace an existing release, a user needs to manually delete the release and trigger this pipeline again.
  * [second phase] it runs validation
    * provision a kvm-enabled VM
    * Pull km-releases repo there
    * Download and unpack the release artifact from km-releases
    * The installation script will run a quick validation when / if all is installed

### Release names agreement

For dev/test, we will use `0.1-test`. When automation is ready, we will keep `0.n-beta` (e.g. `0.11-beta`) as a release created by the process above and triggered with a new tag creation or manually, and `0.n-daily` (e.g. `0.1-daily`) to be replaced on every master update

We will also use other suffixes (e.g. `0.10-demo`) as needed

### Clean up

If for some reason you need to remove a release, here are the steps (using `v0.1-myrelease` as an example):

* Delete release using github UI: https://github.com/kontainapp/km-releases/releases/tag/v0.1-myrelease "delete" button
* Delete local and github tags on KM and km-release repo (`cd` to km on km-releases in your workspace first):
  * git tag -d v0.1-myrelease
  * git push --delete origin v0.1-myrelease
## Install a release

Install instructions are in km-release/README.md

## Payload release

Payloads are released to dockerhub as `runenv` images under `docker.io/kontainapp`. Each payload already creates
`runenv` image. Publishing a release will push the `runenv` image to dockerhub. (not done yet, need to manually `make )

The versioning works differently compare to `km` release versioning. We will have
`runenv-<payload>:latest` and `runenv-<payload>:<SHA>`. Some payloads may have alias to be tagged as well.

For example, python will have the following tag:
1. runenv-python:latest
1. runenv-python:SHA
1. runenv-python-3.7:latest
1. runenv-python-3.7:SHA

Currently supported release: `payload/python` `payload/node` `payload/java`

To perform the final push to dockerhub, we use `publish-runenv-image` target for
each payloads. While one can invoke `make publish-runenv-image` from the root, for
better control, we want to run from each individual directory to avoid
messing up. For example, to release python runenv, run the following:

```bash
# Optional: build the runenv image
make -C payload/python all runenv-image

# Optional: login into dockerhub
make -C cloud/dockerhub login

# Release the runenv-python image to dockerhub
make -C payload/python publish-runenv-image
```

*** END OF DOCUMENT ***
