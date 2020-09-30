# CI/CD pipeline

## Architecture

The purpose is to address two common use cases in development cycle:

1. A developer performs multiple commits in a private branch on Github kontainapp km repository and every individual commit pushed to the repo triggers the CI.

2. A developer creates a pull request in order to merge changes to master and this operation triggers the CI for all changes. The Azure CD pipeline is directly triggered from Github.


## Azure Pipeline

The CI pipeline clones a km repo and runs a containerized build.
It packages build results and tests artifacts into a docker image which is deployed
on Kubernetes.

Tests are deployed as Kubernetes Pods. Pretty much all the work (build / test) is controlled via Makefiles, and the Pipeline just ties them together, and calls `kubectl` where needed to wait, clean up or print the results

**IMPORTANT** We assume kubevirt KVM device plugin is running on Kubernetes (so /dev/kvm is available to containers) and that /dev/kvm has rw access for 'others'.

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

## Login

There are 2 way to login - interactive (`make -C cloud/azure login` and non-interactive (`make -C cloud/azure login-cli`).
Please see `docs/build.md` **Login to Azure** section for more details

## FAQ

### Test failures - where to find the details

If one or more tests fail the **run tests** task will error, and the log there will tell you what is the issue

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
  * Click on `Create and Push KM Test container`. You will see something like `make -C tests testenv-image push-testenv-image IMAGE_VERSION=ci-695 DTYPE=fedora`.
* pull the test image for the correct version, e.g. `make -C tests pull-testenv-image IMAGE_VERSION=ci-695`
* Run container locally `docker run -it --rm --device=/dev/kvm kontain/test-km-fedora:ci-695`
* in the Docker prompt, run tests: `./run_bats_tests.sh`

### How to debug code if it fails on Kubernetes only (and passes locally)

First of all, make sure that

1. kubectl is installed (`sudo dnf install kubernetes-client` on Fedora)
1. you are logged in Azure and Kubernetes (`make -C cloud/azure login`)

Then, find IMAGE_VERSION for your CI run (see above). Let's say this was build 695, so the image version is `ci-695`

* `make -C tests test-withk8s-manual IMAGE_VERSION=ci-695` to run image in Kubernetes
* This will create a pod and  print out the commands to run bash there, as well as the ones to clean up when you are done.

Here is an example of a session:

```sh
[msterin@msterin-p330 tests]$ make  test-withk8s-manual IMAGE_VERSION=ci-695
Creating or updating Kubernetes pod for USER=msterin IMAGE_VERSION=ci-695
Run bash in your pod 'msterin-test-ci-695' using 'kubectl exec msterin-test-ci-695 -it -- bash'
Run tests inside your pod using './run_bats_tests.sh --km=/tests/km'
When you are done, do not forget to 'kubectl delete pod msterin-test-ci-695'
[msterin@msterin-p330 tests]$ kubectl exec msterin-test-ci-695 -it -- bash
[root@msterin-test-ci-695 tests]# ./run_bats_tests.sh  --km=/tests/km
1..38
ok 1 setup_link: check if linking produced text segment where we expect
^C
[root@msterin-test-ci-695 tests] ^D
command terminated with exit code 127
[msterin@msterin-p330 tests]$ kubectl delete pod msterin-test-ci-695
pod "msterin-test-ci-695" deleted
```

Note: a good set of recipes for kubectl interacting with a pod is, for
example,
[here](https://kubernetes.io/blog/2015/10/some-things-you-didnt-know-about-kubectl_28/)

### How to gain access to core file on Kubernetes

If something dropped core while running test on CI (most likely `km`) the core file is retained in `/core` directory on the host.
This directory is also accessible in the CI pod if you running tests manually via `kubectl exec ...` as described above.
In addition there is a separate long living pod named `init-node-cored` that also has access to that directory.
The core files have names template `/core/core.%e.%P.%t` as per `man core.5`.

To access the core files, first run `kubectl get pod | grep init-node-cored` to get the name of the long running pod.
Then you could either

```bash
kubectl exec <pod_name> -it -- /bin/sh
```

and access `/core` from there, or you could do something like:

```bash
kubectl exec <pod_name> -it -- /bin/ls -lh /core
kubectl cp <pod_name>:/core/<specific_file_name> <local_core_name>
```

### Secrets and Pipeline variables

We use service principal extensively in the azure pipeline. To pass the
crendentials of existing service principals, we use the variables in azure
pipeline. For example, you will see variables such as `SP_appId`,
`SP_password`, `SP_tenant`, `SP_displayName`. These are set in the pipeline
UI by design. For reference, follow this:
https://docs.microsoft.com/en-us/azure/devops/pipelines/process/variables

## Nightly CI pipeline

In addition to all the unit test we run in the normal CI pipeline, there are
often tests that takes longer to run. We deploy these tests into a seperate
nightly pipeline that is triggered at the midnight pacific time every night.
Also the nightly pipeline will run on a fresh k8s cluster from Azure AKS. For
details, take a look at `azure-pipelines-nightly.yml`

The nightly pipeline uses the `test-all-withk8s` targets. In the case when
there is no long running test, `test-all-withk8s` will defaults to the same
test as `test-withk8s`. The script that drives these tests are under
`cloud/azure/tests`. It also has a similar `test-all-withk8s-manual` target.

As part of the nightly pipeline, there is a validation target
`validate-runenv-withk8s`. This target uses the `demo-runenv-image`, which
contains a sample application, to validate the runenv image. Since
`runenv-image` contains absolutly the bare minimum, the image is not likely
deployed to the k8s direcly in production. For some payloads such as Nginx or
the Demo Dweb application, the `runenv-image` and the `demo-runenv-image`
will be the same, since the payload is the application itself.

### Pull Request to change the nightly pipeline.

Due to the time needed to run the nightly pipeline, it is not triggered
automatically when PR or commits take place. After opening a PR, we should
manually run the nightly pipeline to make sure everything looks OK.