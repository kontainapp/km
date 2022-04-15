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

## How test are run om VMs

Some jobs are ran on github hosted runners, such as `ubuntu-latest`.
However these runners are weak and don't support KVM,
so some steps are ran on on-demand runners on AWS and Azure.

There are two jobs in the `km-ci-workflow.yaml` file, `start-runners` and `stop-runner`.
They are like a pair of parenthesis.

`start-runners` launches two VMs, one on EC2 and one on Azure.
There are a lot of parameters there.
They specify how to deal with our accounts on the clouds, which image to use,
and also how to connect to our github repo.
These are likely going to stay unchanged.

Once the VMs start they execute a script that registers them with gihub as runners.
The names (labels) of the runners are output parameters of the step encoded as run-ons associative array by cloud.

`stop-runner` analyses the status of the workflow - success, failure, cancelled, skipped.
For failure it stops the VMs and keeps them around for further human analysis, otherwise VMs are destroyed.

The actual test jobs, `km-test`, `kkm-test-vms` and potentially new ones use `runs-on:` parameter to specify the runner to use.
The rest of the test jobs follow the usual action syntax.
The test steps are executed on the appropriate runner.

### Outcome

A test job run will have ONE of the following outcomes,
success, failure, or cancelled.
In case of failure the VMs that ran the action runners are stopped but kept so the failure could be analyzed.
They are cleaned up after 7 days.

## Login and How to debug

There is no need for additional login to run or see and analyze CI logs.

Failed tests leave VMs around, available to start and ssh in to.
They are accessible on EC2 instances or Azure VMs pages.
To analyze failed tests it is necessary to locate the action runner VMs.
The VMs are label as `{ec2,azure}-runner-`*Pipeline_Name*`-`*run_number*, eg `ec2-runner-KM-CI-Pipeline-1284`.
Start the VM, then ssh into it using public IP number.
The user name is kontain, the public ssh keys are taken from cloud/azure/ssh directory - please put yours there.
The VMs are kept there for a week, after that they are erased.

The CI jobs are run in `/action_runner/_work` directory.

Please stop the VM when you done.

It is also possible to create an entirely new VM using the base image,
and run all the steps manually.

Not that ssh keys are put in the CI VMs when they start, not when image is created.
To ssh in this new VM use the cloud provided ssh key mechanisms.

## A note about Quotas on Azure

Before going further, it is important to understand that every test run uses VMs from Azure and AWS. We usually use VMs with 4vCPU. That means that parallel run of multiple PRs can exceed Kontain account quota (currently increased to 48 vCPU at a time - we can set it higher) and the CI will fail with "Deployment failure" message from Azure.

### Base image for tests

Pre-created AMI and Azure base image named L0BaseImage are used.
On Azure that image is in `auto-github-runner` resource group.
Base images are created in `cloud/azure` and `cloud/aws` using packer under the hood:
```
make -C cloud/azure L0BaseImage
make -C cloud/aws L0BaseImage
```

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
They are all configured in github Kontainapp Organization Secrets and accessed as `${{ secret.xxx }}` in CI configuration.

This process needs SP_* environment vars are set to authenticate to Azure (see build.md doc, 'login-cli' chapter).
It also needs environment vars (or secrets, if running in CI).

* GITHUB_TOKEN - personal token authoring clone/checkout from github
* VAGRANT_CLOUD_TOKEN - personal token authorizing delete/create boxes on vagrantcloud for username `kontain`, password is Kontain WiFi password `SecureF...`. In CI, it the token is already in the secrets for github.com/kontainapp.

## Nightly CI pipeline

In addition to all the unit test we run in the normal CI pipeline, there are
additional tests that takes longer to run, e.g. full Node test suite.
 They are configured in the same workflow, but only enabled if the workflow is started on schedule or manually (not on pull request).
Full pass with long test is scheduled to run at the midnight pacific time every night.

The nightly pipeline uses the `test-all-withdocker` make targets.
In case when there is no long running test,
`test-all-withdocker` will defaults to the same test as `test-withdocker`.
