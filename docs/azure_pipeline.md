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

#####Outcome

A test job run will have ONE of the following outcomes, either runs to completion if all tests passed OR times out.


## Build triggers

The CI/CD build pipeline has two types of triggers:

```
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


### Test failures

If one or more tests fail the **run tests** task will error.
The **tests results** task is the one you want to look at for tests output and time info.

From github:
* click on **Show all checks** then **Details**
* then click on 'Errors' to get to the azure devOps build detailed logs


### Skip CI for individual commits

Include [skip ci] in the commit message or description of the HEAD commit and Azure Pipelines will skip running CI.

### Edit to CI script

If you want changes to CI, you can edit script in your private branch and go through standard PR process

CI source is under source control if you have questions/confusion - please ask @ slack **build\_and\_test** channel, and PLEASE do not not hesitate to update this file with the info :-)

### Update buildenv images

    make -C ./cloud/azure buildenv      # default to fedora
    make -C ./cloud/azure buildenv DTYPE=ubuntu|fedora|alpine
