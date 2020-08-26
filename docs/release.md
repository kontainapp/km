# KM releases

## What's a release

A release is Github collection of sources (autopackaged by github) and extra files (manually or script-added to the "release files) hosted on github.
We create releases in km-release repo so KM sources are not released. For KM releases, we create a tagged release using github, and `manually` upload .tar.gz files with the content built in `km` repo.

A release may include multiple .tar.gz file , but there is at least one - `kontain.tar.gz` with the KM binaries, libs and tools, which is created, and is expected to be installed before other tars. All files related to release are expected to be on gihub, with the exception of Docker images which can be on dockerhub

For now we will hard-code release tag as 0.1-test for testing phase, and 0.1-beta when we are ready to release. The reason is that a release is a undeletable read only snapshot, we can only add files to it.

## How to create a release

Release content is described in docs/planning/m1.


We maintain releases on [github km-releases](https://github.com/kontainapp/km-releases/releases/) . We also maintain install script and basic install documentation there. This repo is a submodule to KM and is sitting in ./km-releases (after 'git submodule sync --init)

Everything else (including release documentation) is kept in [github km](https://github.com/kontainapp/km)

Note that km-release is public repo, so no authentication is needed for read access.

Each payload may decide to implement `release` make target which needs to package files. The format, location and process is up to a payload but install process needs to be documented in km-releases/README.md. If files need to be uploaded to github (e.g. we can decide to use dockerhub for packaging a specific payload in which case no upload to github is needed) , wo do it

To build a release, use `make release` from the appropriate dir (e.g. top-level `make release` will scan all dirs and try to build `release` target here).
For payloads (or tests, or any other dir scanned from top) we assume that KM (and related libraries) are already installed and /opt/kontain exists.

Release targets do the following:

* Create a tar.gz using make.
* Edit 0.1-test release on https://github.com/kontainapp/km-releases/releases , and update kontain.tar.gz from build/kontain.tar.gz or add your payload tar.gz
  * This, as well as tag name creation and link to specific SHA on KM, will be automated later
* Validate the release by following install instruction on https://github.com/kontainapp/km-releases
  * if needed, update the instructions there, in your branch - follow regular GIT processes for submodules

# Install

Install instructions are in km-release/README.md

# TODO

* create_release should re-tag KM sources, and add release to tar name
* kontain_install should accept try to use the latest release (using GIT API) , overridable by env var
* releases repo should have installation instruction (when the above is clear)
* add test for the doc (basic). Going forward - need to build docs with m4 by including scripts ? and testing these scripts directly


## Questions

* We need a license for releases. For now I put MIT there (no liability)
* now is the last chance to switch to python from bash :-)  for install
* there is no automation to tag something in KM (SHA), tag something in payload (SHA for our code an version of payload) and generate a release. It's OK to do stuff manually for now but if/when we need to keep going it needs to be re-designed