# CI/CD pipeline

## Architecture

The purpose is to address two common use cases in development cycle:

1. A developer performs multiple commits in a private branch on Github kontainapp km repository and every individual commit pushed to the repo triggers the CI.

2. A developer creates a pull request in order to merge changes to master and this operation triggers the CI for all changes. The Azure CD pipeline is directly triggered from Github.


## GitHub Workflows

The GitHub Workflows CI is fully configured by files in .github/workflows. See Github docs for more information.

Build results are packaged into a docker images.
Tests are deployed on VMs in on-demand self-hosted github runners.
Pretty much all the work (build / test) is controlled via Makefiles, and the action workflow just ties them together.
Tests are run by `make test-withdocker`, `make test-all-withdocker`, and `make validate-runenv-images`.

### Outcome

A test job run will have ONE of the following outcomes,
success, failure, or cancelled.
In case of failure the VMs that ran the action runners are stopped but kept so the failure could be analyzed.
They are cleaned up after 7 days.

## Login

There is no need for additional login to run or see and analyze CI logs.

To analyze failed tests it is necessary to locate the action runner VMs.
The VMs are label as `{ec2,azure}-runner-`*Pipeline_Name*`-`*run_number*, eg `ec2-runner-KM-CI-Pipeline-1284`.
Start the VM, then ssh into it using public IP number, username `kontain`,
and one of the ssh public keys placed in `cloud/ssh` directory.

The tests are run in `/action_runner/_work` directory.

Please stop the VM when you done.

## FAQ

### How to run my own workflows

It's often useful to have your own workflows for debugging specific issues. You can create a new file in .github/workflows which only have CI steps you care about, and is triggered on push to your branch. Github will create a new pipeline and run it on each push to your branch.
### How to change CI script

If you want changes to CI, you can edit script in your private branch and go through standard PR process

CI source is under source control if you have questions/confusion - please ask @ slack **build\_and\_test** channel, and PLEASE do not not hesitate to update this file with the info :-)

### How to update buildenv images

```sh
    make -C tests buildenv-image
    make -C tests push-buildenv-image
```

Note that push is protected (see Makefiles)

### How to run my tests without CI, but in the same container

* Find the image tag for your CI run:
  * click on Checks tab in the PR
  * click on `Show all checks`
  * click on 1st `Details`
  * Click on `Create and Push KM Test container`. You will see something like `make -C tests testenv-image push-testenv-image IMAGE_VERSION=ci-695 DTYPE=fedora`.
* pull the test image for the correct version, e.g. `make -C tests pull-testenv-image IMAGE_VERSION=ci-695`
* Run container locally `docker run -it --rm --device=/dev/kvm kontainapp/test-km-fedora:ci-695`
* in the Docker prompt, run tests: `./run_bats_tests.sh`

### How to debug code if it fails on CI only (and passes locally)

See [Login](#login) above.

All of the files and results of the test runs are on that machine,
including core dumps if any.

Please stop the VM when you done.

### Secrets and Pipeline variables

We use service principal extensively to access azure resources.
We also use AWS and Github tokens.
They are all configured in github Kontainapp Organzation Secrets and accessed as `${{ secret.xxx }}` in CI configuration.

## Nightly CI pipeline

In addition to all the unit test we run in the normal CI pipeline, there are
additional tests that takes longer to run, e.g. full Node test suite.
 They are configured in the same workflow, but only enabled if the workflow is started on schedule or manually (not on pull request).
Full pass with long test is scheduled to run at the midnight pacific time every night.

The nightly pipeline uses the `test-all-withk8s` Make targets. In the case when
there is no long running test, `test-all-withk8s` will defaults to the same
test as `test-withk8s`. The script that drives these tests are under
`cloud/azure/tests`. It also has a similar `test-all-withk8s-manual` target.
