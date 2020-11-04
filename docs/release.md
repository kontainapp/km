# KM releases

## What's a release

A release is Github collection of sources (auto-packaged by github) and extra files (manually or script-added to the "release files) hosted on github.

We create releases in public kontainapp/km-release repo so KM sources are not released but KM binaries can be picked up for evaluation.

A release may include multiple .tar.gz file , but there is at least one - `kontain.tar.gz` with the KM binaries, libs and tools, which is created, and is expected to be installed before other tars. All files related to release are expected to be on gihub, with the exception of Docker images which can be on dockerhub.

## How to create a release

Release content is described in `docs/planning/*.md`.

We maintain releases on [github km-releases](https://github.com/kontainapp/km-releases/releases/) . We also maintain install script and basic install documentation there. This repo is a submodule to KM and is sitting in ./km-releases (after 'git submodule sync --init)

Everything else (including release documentation) is kept in [github km](https://github.com/kontainapp/km)

Note that km-release is public repo, so no authentication is needed for read access.

Each payload may decide to implement `release` make target which needs to package files. The format, location and process is up to a payload but install process needs to be documented in km-releases/README.md. If files need to be uploaded to github (e.g. we can decide to use dockerhub for packaging a specific payload in which case no upload to github is needed) , wo do it

To build a release, use `make release` from the appropriate dir (e.g. top-level `make release` will scan all dirs and try to build `release` target here).
For payloads (or tests, or any other dir scanned from top) we assume that KM (and related libraries) are already installed and /opt/kontain exists.

### Steps

For now we hard-code default release tag as `v0.1-test` for automation dev and testing phase. When a release with this tag is created, it automatically overrides the prior one with the same tag.

If any other tag is used and a release with this tag already exists on km-releases repor, publishing of a release will fail.

We recommend using unique tags, e.g.  `v0.10-beta-demo-nokia` for manual uploads of anything we demo or give to people to try.

1. `make release runenv-image` to build release tarball(s) and payload run environment images
1. Publish the release on github using `RELEASE_TAG=v0.1-test make -C km publish-release`. (note: manual run requires GITHUB_RELEASE_TOKEN env var with github personal access token).
1. Publish runenv images to dockerhub with `make publish-runenv-image`
1. Validate the release by following install instruction on https://github.com/kontainapp/km-releases. Note: on CI the release is auto-validated.

### CI automation

We use existing Azure pipeline mechanism to generate releases. A trigger
for the release is creating `v*` version tag in KM. For example:

```bash
git tag v0.1-test-manual; git push origin v0.1-test-manual
```

We also auto-trigger the same release pipeline for any bracnh named 'releases/*/* , e.g. `releases/beta/snapshot-api`
The release pipeline builds , publishes and validates the release. See steps in `azure-pipeline-release.yml`

Here is the use case:

* We decide a specific commit in master or release branch is good enough for release.  The commit should be tested by existing pipelines already
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
    * run (new) `validate.py` script to execute steps described in GettingStarted.md
    * `NOTE`: this is phase 2, the 1st pass is just get it to build and push

### Release names agreement

For dev/test, we will use `0.1-test`. When automation is ready, we will keep `0.n-beta` (e.g. `0.11-beta`) as a release created by the process above and triggered with a new tag creation or manually, and `0.n-daily` (e.g. `0.10-daily`) to be replaced on every master update

We will also use other suffixes (e.g. `0.10-demo`) as needed

## Install

Install instructions are in km-release/README.md

## TODO

* P0: Discuss the whole process with the team, I am sure there are usability glitches - and ask to try with v0.1-alias-test
* P1: Test more than 'hello world' for a release
* P1: A mechanism to clean up stale releases, force-push (or delete a specific release)

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
messing up. For example, to release python runenv, run:


```bash
# Optional: build the runenv image
make -C payload/python all runenv-image

# Optional: login into dockerhub
make -C cloud/dockerhub login

# Release the runenv-python image to dockerhub
make -C payload/python publish-runenv-image
```
