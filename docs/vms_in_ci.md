# VMs and VM images in Kontain CI and dev processes

## Background

Kontain process and CI is build around docker images (buildenv/testenv/runenv) and running tests on physical or cloud based machines.

## How test are run

We use on-demand self-hosted github action runners.
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

### How to debug it

Failed tests leave VMs available to start and ssh in to.
The names on the VMs are derived from the action name and run number: `{ec2,azure}-runner-KM-CI-Pipeline-1279`,
they are accessible on EC2 instances or Azure VMs pages.
The user name is kontain, the public ssh keys are taken from cloud/azure/ssh directory - please put yours there.
The VMs are kept there for a week, after that they are erased.

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
### Vagrant boxes for release

Vagrant boxes and AMI for release are also created and uploaded to vagrantcloud using Packer. Invocation is wrapped in `make`.
All files are in tools/hashicorp. Make targets:

* `make -C tools/hashicorp ami` - build AMI with Ubuntu and preinstalled KM (based on results of 'make release' and 'make kkm-pkg')
* `make -C tools/hashicorp vm-images` - build 2 boxes (fedora based and ubuntu based) with presinstalled KM
* `make -C tools/hashicorp register-boxes` - register freshly build boxes with local `vagrant box` command
* `make -C tools/hashicorp upload-boxes` - upload freshly build boxes to vagrant cloud. Expects VAGRANT_CLOUD_TOKEN env to be set

also `test-box-upload` and `product` are useful goals there - take a look

#### CI for vagrant boxes and AMI creation

No CI support for this yet. There are issues https://github.com/kontainapp/km/issues/1276 and https://github.com/kontainapp/km/issues/1196 for resurrecting `make release`, hooking it to nightly build, and adding box/ami creation there.

#### Credentials

This process needs SP_* environment vars are set to authenticate to Azure (see build.md doc, 'login-cli' chapter).
It also needs environment vars (or secrets, if running in CI)

* GITHUB_TOKEN - personal token authoring clone/checkout from github
* VAGRANT_CLOUD_TOKEN - personal token authorizing delete/create boxes on vagrantcloud for username `kontain` , password is Kontain WiFi password `SecureF...`. In CI, it the token is already in the secrets for github.com/kontainapp.

