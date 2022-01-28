# VMs and VM images in Kontain CI and dev processes

## Background

Kontain process and CI is build around docker images (buildenv/testenv/runenv) and generally running tests on Kubernetes.
There were multiple reasons for that, mainly the original focus on Kubernetes and desire to have it integrated from get go.

With time, we had to add multiple VM-based test - e.g. KRUN and AWS/KKM. We also started to package release as VM (AMI and Vagrant Box).
It created complicated and fragile `az` and `aws` CLI based scripts, which organically started to move to using Hashicorp Vagrant and Package tools to manage all VM-related operations.

The process of converting all VM-related ops to packer/vagrant is multi-staged. This document covers a few discussions and a status of related code...c The discussions are as follows:

* CI - coverage, packer usage
* milestones for KM/KKM conversion
* Base VM images, file location

## A note about Quotas on Azure

Before going further, it is important to understand that every test run uses VMs from Azure and AWS. We usually use VMs with 4vCPU (defined in packer config files). That means that parallel run of multiple PRs can exceed Kontain account quota (currently increased to 48 vCPU at a time - we can set it higher) and the CI will fail with "Deployment failure" message from Azure.

This is not specific for this PR, I simply stepped into this while doing accidental series of pushes and triggering a dozen or so CIs in flight

## Make and CI for testing - support for packer

New targets in images.mk are introduced - `test-withpacker`, `test-all-withpacker` and `validate-runenv-image-withpacker`. As such, in all places supporting `test`, `test-all` and `validate-runenv-image` targets now support `<target>-withpacker`.

These targets runs `packer` to provision a VM in AWS or Azure based on pre-built AMI or Azure Image, and then run test steps inside of this VM using regular `make`, e.g. `make test-all`. This process "pretends" to build a new image; but we only need the process (which is *running test*) to succeed and do not need the target image itself, so it is discarded.

Internally, the packer will create a VM, check out the KM repo, pull in a testenv or runenv image and run the tests.

For pulling the image, IMAGE_VERSION env is *mandatory*. You can always push your own version to be used. e.g. `make runenv-image push-runenv-image IMAGE_VERSION=ci-my-tag` (use `ci-` prefix so the image will be auto-cleaned up by CI during the run of ` make -C cloud/azure ci-image-purge`) , or simply use the ones produced in CI runs.

Examples:

* `make -C tests test-withpacker IMAGE_VERSION=ci-219`
* `make -C payloads/node test-all-withpacker IMAGE_VERSION=ci-240`
* `make -C payloads validate-runenv-image-withpacker IMAGE_VERSION=ci-200`

Packer config for this is in `tests/packer/km-test.pkr.hcl` and the actual test script is in `tests/packer/scripts/km-test.bash`.

The script/targets are controlled by env vars which are passed by the packer to the km-test.sh script running in the newly provisioned VM. All of them MUST exist:

* IMAGE_VERSION - version of image to validate. MUST be passed to make
* HYPERVISOR_DEVICE - /dev/kvm or /dev/kkm. Default is /dev/kvm
* SP_* - auth info for Azure. Are pre-set when you configure 'make -C cloud/azure login-cli`. See build.md doc
* GITHUB_TOKEN - token to check out KM repo - during CI, received from github. When running manually, set it to a personal github token (see Account->Setting->DevOptions->PersonalTokens in Github UI)
* SRC_BRANCH - current branch, used for KM checkout in the Packer VM. Default is the current branch

* PACKER_DIR - if this is passed, the make *inside* the packer-managed VM will happen there. It is done to allow "scanning" of dirs inside, not outside of packer. E.g. `make -C payloads/busybox test-withpacker PACKER_DIR=payloads ...` with run ONE packer VM and run make there in payloads dir, which will do the scanning of subdirs.  It's a hack to avoid the cost of running multiple packer-managed VMs for the same - i.e. `make -C payloads test-withpacker ...` with run packer for EACH subdir.

See the script and packer config for more details

### Base image for tests

Currently pre-created AMI and Azure base image are used. Base Azure image is created in cloud/azure (see below). AMI is crested by a script written @sv631.... there is an AMI creation packer process, it is not used here yet.

### How to debug it

The easiest way to debug is to create a VM on Azure or AWS using the same image, ssh there, set the GITHUB_TOKEN and then replay km-tests.sh

To create, list and manage VMs using base images, there are helper scripts and targets - see `cloud/azure/azure_vm.sh` for Azure and `tools/hashicorp/.run-instance`, `describe-instances` and `terminate-instances` for AWS.
These were written as convenience wrappers to avoid az/aws; a few more details are below.

#### Azure

cloud/azure/azure_vm.sh is a helper script. Usage examples

```bash
./cloud/azure/azure_vm.sh ls # list all VMs
VM_IMAGE=L0-base RESOURCE_GROUP=PackerBuildRG ./cloud/azure/azure_vm.sh create myVm # you can ssh with user Kontain
RESOURCE_GROUP=PackerBuildRG ./cloud/azure/azure_vm.sh delete myVm
```

### AWS

This was a helper for old AMI "release" creation. It assumes AMI_name=Kontain_ubuntu_20.04 (see run-instance.sh).
It is invoked by `make -C tools/hashicorp .run_instance` , `.terminate-instances` and `.describe-instances`

It actually needs to be moved to cloud/aws/aws_vm.sh (or simple vm_manage.sh) and make consistent with azure
As of now, it can be used by making AMI_Name configurable, and/or using as a set of examples.

## VM images - base image, release/Vagrant box build

### BaseImages

Commands:

* ` make -C cloud/azure L0-image L0_VMIMAGE_NAME=L0BaseImage`
* TBD: `make -C cloud/aws/ L0-image`

The base image is built using the following config  files

* cloud/azure/L0-image.pkr.hcl - build BaseImage on Azure. Includes packer, vagrant, virtualbox, multiple tools and a couple of pre-loaded Vagrant Boxes.
  * TBD: add dev tools, remove pre-loaded Vagrant boxes. Dev tools will allow to use this image in more use cases. Vagrant boxes need to be place in a next level (L1) base image, to be used to produce Vagrant boxes with pre-installed Kontain release
* TBD: cloud/aws/L0-image.pkr.hcl - build BaseImage AMI on AWS . This should replace the home grown script for base AMI creation.

There is also a pipeline that automatically validates the build, and generates L0BaseImage-validation image.
Verbatim from comments in `.github/workflows/L0-packer-build.yaml`:

```txt

# FOR ACTUAL L0 IMAGE TO BE REPLACED, EITHER CHANGE L0_IMAGE_NAME BELOW OR
# OR RUN THE MAKE MANUALLY
# Manual run: make -C cloud/azure L0-image L0_VMIMAGE_NAME=L0BaseImage
# manual runs expect 'make -C cloud-azure login-cli' to be successful

# Reason: Azure currently does not allow to rename images, AND existing image is deleted before the
# packer can start creation of a new one. So if this build fails, ALL operations which need the L0 base
# image will fail. More that that, CI can race so the other ops would fail while this one is being
# successfully run
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

## Phases on moving to easier VM management and KM testing

0. PR https://github.com/kontainapp/km/pull/1275 with packer files for test, building base images, and this document
0a. Add some tests-withpacker to CI in parallel with existing tests - and then drop existing/k8s as needed

1b. Adjust / document debug process for the above

* keep or not VMs on failure
* clean up old stopped VM
* handle ssh keys or well-known passwords
* document how to run/stop/etc and ssh to the VMs

2a. - Move krun unit test to packer ****
    - clean up runenv to support krun (crun may require flags, and without flags should fail gracefully/ clear err msg)
    - validate-runenv for payloads RUNTIME=/opt/kontain/bin/krun
    - potentially: more tests for runenv (taken from payload test suites and added on top of runenv)

=== KRUN on Kubernetes: assumes integration done

3.0 run test from 2a to Kubernetes with KRUN
3a Use new cluster each time - since we create it FASTER than compile/push testenv. Update docs
3b Drop K8s test from daily runs, update guidances.
3c Add krun to cluster. Use the above to test K8s krun integration ( new cluster each time - ~3min to create a cluster)

## VM Base images - going forward

Generally, seem to need base Images support for:

* KKM test on Azure and AWS
* CI run of packing Vagrant box
* Basic VM to validate Release box and release bundle (subset of the above)
* CRUN test on both AWS and Azure

Ideally, a few basic images needs to be created (as AMI and as Azure image). The key issue with having one gigantic base image is that it will get hard to rebuild all when it changes. So I'd recommend having 2 or 3:

* L0-base - base image with dev tools and all key packages, so the majority of stuff would run on it, including kubectl, docker, aws and az CLI.
* L1-packer-base - L0 + packer/vagrant/virtualbox. Adds 1.5GB and harder to rebuild.
* L1-vagrant-base - L1 + a few pre-loaded boxes for acceleration

L0 should be good for all except building a pre-packages Vagrant Boxes for releases.
As of now, only L0 is created so the above is more of a spec.

### Base images and CI

Each base image can have a dedicated pipeline to rebuild it if it correspondent hcl file changes in main.

L0-packer-build.yaml introduces it for L0 base image. Note that Azure does not allow to rename images and in order to start building an image the old one with the same name needs to be deleted, so if a build fail the base image can dissappear thus failign all CI runs. So be careful - see `L0-packer-build.yaml` for some comments

## Current locations of VM-related stuff built with packer

Packer config for running tests in Packer

```txt
./tests/packer/km-test.pkr.hcl
./tests/packer/Makefile
./tests/packer/scripts/km-test.sh
```

Packer configs for creating vagrant box and AMI with KM preinstalled, and uploading it (used by `make -C tools/hashicorp ...` )..

```txt
./tools/hashicorp/aws_ami_templates/ubuntu-aws.pkr.hcl
./tools/hashicorp/aws_ami_templates/config.pkr.hcl
./tools/hashicorp/test_box_upload.pkr.hcl
./tools/hashicorp/box_images_templates/ubuntu2004.pkrvars.hcl
./tools/hashicorp/box_images_templates/build_km_images.pkr.hcl
./tools/hashicorp/box_images_templates/fedora32.pkrvars.hcl
./tools/hashicorp/test_ami_upload.pkr.hcl
```

Packer config for build L0 image for azure, and running large Azure box with nested Packer to produce vagrant boxes with pre installed KM ()see above)

```txt
./cloud/azure/L0-image.pkr.hcl - Generate L0 (very botttom) BaseImage on Azure.
./cloud/azure/vagrant-box.pkr.hcl - Run a big VM based on L0image, and inside of it run vagrant box creation
```

## TODO: Missing components / going forward

**Going forward** - some of the final polish is missing:

* AWS: need to replace manual base AMI creation (for KM/KKm test) with Packer config
* AWS: drop specific AMI id and use AMI "data" to fetch the id.
  * See ubuntu-aws.pkr.hcl for an example; this file is used to build AMI with pre-installed KM/kkm for customers
* Replace fragile and LONG test_remote.py and test_local.py for KRUN test with Packer config
* Review CI and drop uneeded test runs. Use more `-withpacker` targets there
  * potentially using strategy/matrix support in CI to run identical steps on different location/platforms (see https://docs.github.com/en/actions/reference/workflow-syntax-for-github-actions#jobsjob_idstrategy)

==== END OF THE DOCUMENT =====



