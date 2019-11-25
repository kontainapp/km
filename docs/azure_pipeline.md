# CI/CD pipeline

## Architecture

The purpose is to address two common use cases in development cycle:

1. A developer performs multiple commits in a private branch on Github kontainapp km repository and every individual commit pushed to the repo triggers the CI.

2. A developer creates a pull request in order to merge changes to master and this operation triggers the CI for all changes. The Azure CD pipeline is directly triggered from Github.


## Azure Pipeline

The CI pipeline clones a km repo and runs a containerized build.
It packages build results and tests artifacts into a docker image which is deployed
on Kubernetes.

Tests are deployed as Kubernetes Job.
A test job will either run to completion if all tests passed or otherwise timeout.

If you have test failure(s):
On azure the 'run tests' task is the task that fails.
This is because the failure is asserted by the kubernetes job not running successfully to completion and timing out.

But the task of interest is the subsequent 'get tests results' task which displays the tests output.

One can navigate easily between tasks using Previous / Next task blue arrows on upper right corner.

It is also possible to download the CI logs as text files.

### Outcome

A test job run will have ONE of the following outcomes, either runs to completion if all tests passed OR times out.

### Build triggers

The CI/CD build pipeline has two types of triggers:

```yaml
trigger:        <<<<<<<<<<< CI triggers are defined here.
- jme/tests
pr:             <<<<<<<<<<< PR triggers are defined here.
- master
```

In the above yaml

A PR will trigger a build for pull requests targeting 'master' branch and subsequent updates to the PR.

There is no need to specify source branches: The PR trigger specifies the target branch 'master'.

Commits pushed to branch jme/tests will trigger the CI pipeline.

## FAQ

### Test failures - where to find the details

If one or more tests fail the **run tests** task will error.
The **tests results** task is the one you want to look at for tests output and time info.

From github:

* click on **Show all checks** then **Details**
* then click on 'Errors' to get to the azure devOps build detailed logs

### How to skip CI for individual commits

Include [skip ci] in the commit message or description of the HEAD commit and Azure Pipelines will skip running CI.

### How to change CI script

If you want changes to CI, you can edit script in your private branch and go through standard PR process

CI source is under source control if you have questions/confusion - please ask @ slack **build\_and\_test** channel, and PLEASE do not not hesitate to update this file with the info :-)

### How to update buildenv images

```sh
    make -C tests buildenv-image
    make -C tests push-buildenv-image
```

Note that push is protected (see Makefiles)

### How to run my tests without Kubernetes, but in the same container

* Find the image tag for your CI run:
  * click on Checks tab in the PR
  * click on `Show all checks`
  * click on 1st `Details`
  * Click on `Create and Push KM Test container`. You will see something like `make -C tests testenv-image push-testenv-image IMAGE_VERSION=CI-695 DTYPE=fedora`.
* pull the test image for the correct version, e.g. `make -C tests pull-testenv-image IMAGE_VERSION=CI-695`
* Run container locally `docker run -it --rm --device=/dev/kvm kontain/test-km-fedora:CI-695`
* in the Docker prompt, run tests: `./run_bats_tests.sh-`

### How to debug code if it fails on Kubernetes only (and passes locally)

First of all, make sure that

1. kubectl is installed (`sudo dnf install kubernetes-client` on Fedora)
1. you are logged in Azure and Kubernetes (`make -C cloud/azure login`)

Then, find IMAGE_VERSION for your CI run (see above). Let's say this was build 695, so the image version is `CI-695`

* `make -C tests make  kubernetes-test IMAGE_VERSION=CI-695` to run image in Kubernetes
* This will create a pod and  print out the commands to run bash there, as well as the ones to clean up when you are done.

Here is an example of a session:

```sh
[msterin@msterin-p330 tests]$ make  kubernetes-test IMAGE_VERSION=CI-695
Creating or updating Kubernetes pod for USER=msterin IMAGE_VERSION=CI-695
Run bash in your pod 'msterin-test-ci-695' using 'kubectl exec msterin-test-ci-695 -it -- bash'
Run tests inside your pod using './run_bats_tests.sh'
When you are done, do not forget to 'kubectl delete pod msterin-test-ci-695'
[msterin@msterin-p330 tests]$ kubectl exec msterin-test-ci-695 -it -- bash
[root@msterin-test-ci-695 tests]# ./run_bats_tests.sh
1..38
ok 1 setup_link: check if linking produced text segment where we expect
^C
[root@msterin-test-ci-695 tests] ^D
command terminated with exit code 127
[msterin@msterin-p330 tests]$ kubectl delete pod msterin-test-ci-695
pod "msterin-test-ci-695" deleted
```

Note: a good set of recipes for kubectl interacting with a pod is, for example, [here](https://kubernetes.io/blog/2015/10/some-things-you-didnt-know-about-kubectl_28/)
