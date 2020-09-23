# KM releases

## What's a release

A release is Github collection of sources (auto-packaged by github) and extra files (manually or script-added to the "release files) hosted on github.

We create releases in public kontainapp/km-release repo so KM sources are not released but KM binaries can be picked up for evaluation.

A release may include multiple .tar.gz file , but there is at least one - `kontain.tar.gz` with the KM binaries, libs and tools, which is created, and is expected to be installed before other tars. All files related to release are expected to be on gihub, with the exception of Docker images which can be on dockerhub

## How to create a release

Release content is described in docs/planning/m1.

We maintain releases on [github km-releases](https://github.com/kontainapp/km-releases/releases/) . We also maintain install script and basic install documentation there. This repo is a submodule to KM and is sitting in ./km-releases (after 'git submodule sync --init)

Everything else (including release documentation) is kept in [github km](https://github.com/kontainapp/km)

Note that km-release is public repo, so no authentication is needed for read access.

Each payload may decide to implement `release` make target which needs to package files. The format, location and process is up to a payload but install process needs to be documented in km-releases/README.md. If files need to be uploaded to github (e.g. we can decide to use dockerhub for packaging a specific payload in which case no upload to github is needed) , wo do it

To build a release, use `make release` from the appropriate dir (e.g. top-level `make release` will scan all dirs and try to build `release` target here).
For payloads (or tests, or any other dir scanned from top) we assume that KM (and related libraries) are already installed and /opt/kontain exists.

### Current process

For now we will hard-code release tag as `v0.1-test` for automation dev and testing phase,
and `v0.10-beta` for manual uploads of anything we demo or give to people to try.

1. Build release tarball using `make -C km release` in KM repo
1. Push the release using `RELEASE_TAG=v0.1-test make -C km push_release`.
1. Add other payload tar.gz if needed (payloads are scanned durig the `make release` and may generate their own tarballs)
1. Validate the release by following install instruction on https://github.com/kontainapp/km-releases
1. if needed, update the instructions there, in your branch - follow regular GIT processes for sub-modules

### Planned automation (target - mid-to-late September)

We will use existing Azure pipeline mechanism to generate releases. A trigger for the release is creating a release/version branch in KM.

Here is the use case:

* We decide a specific commit in master or release branch is good enough for release.  The commit should be tested by existing pipelines already
* We decide to name this release, say `v0.9-beta`
* We create a tag `v0.9-beta` on the commit and push the tag
  * when/if we need a branch to work on patches, we will create `releases/v0.9-beta` and will be tagging it separately for sub-releases, e.g. `v0.9.1-beta`
  * Note, in git, a tag is independent of branch and associated only with a commit. Therefore, when a tag is pushed, the pipeline will be triggered.
* This trigger a new Azure pipeline on version `v*` tags to generate and push the artifacts
  * Thus pipeline uses standard pipeline components to build the product
  * It invokes `make release` to generate release artifacts
  * It invokes `make push-release` to call a (new) script using github API
    * The script uses the current branch name to identify release tag
    * It creates a release (say `v0.9-beta`) and pushes the artifacts there.
    * `NOTE` this requires a github Personal Access Token (PAT).
    * `NOTE2` if a release already exists, the release script will fail. If the intent is to replace an existing release, a user needs to manually delete the release and trigger this pipeline again.
  * [second phase] it runs validation
    * provision a kvm-enabled VM
    * Pull km-releases repo there
    * Download and unpack the release artifact from km-releases
    * run (new) `validate.py` script to execute steps described in GettingStarted.md
    * `NOTE`: this is phase 2, the 1st pass is just get it to build and push

### Release names

For dev/test, we will use `0.1-test`. When automation is ready, we will keep `0.n-beta` (e.g. `0.11-beta`) as a release created by the process above and triggered with a new tag creation or manually, and `0.n-daily` (e.g. `0.10-daily`) to be replaced on every master update

## Install

Install instructions are in km-release/README.md

## TODO

* kontain_install.sh should accept try to use the latest release (using GIT API) , overridable by env var
* add test for the doc (basic). Going forward - need to build docs with m4 by including scripts ? and testing these scripts directly

## Questions

* We need a license for releases. For now I put MIT there (no liability)

## Languages

* Turns out, the release is more of a tool than script, so writing it in Golang, as oppose to python3.