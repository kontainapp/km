# CI/CD pipeline 

## Architecture
* A developer changes application source code
* A developper pushes changes to Github kontainapp km repository or create pull request
* Github triggers Azure CD pipeline


## Azure Pipeline

* Checkout kontainapp km repository

* Pull km-buildenv-fedora from kontainkubecr

* Build km with with docker

* Create a test-bats docker image 

* Generate k8s job deployment for the build 

* Deploys test job on k8s cluster

* Waifor completion (pass), timeout on failure(s)

	* runs bats km\_core\_tests.bats

	* prints time_info

* Get test job logs 

* Delete job


## Build triggers

The CI/CD build pipeline has two types of trigger:

* CI trigger can cause a build to run whenever a push is made to the specified branches or tag
* PR trigeer to trigger a build to run whenever a pull request is opened 


## FAQ


### Trigger the pipeline on every commits

If you want to use the pipeline without having to create a PR with [DO NOT MERGE], you can edit the azure-pipeline.yaml file and your branch under trigger:
```
trigger:
- jme/tests
-   <<<<<<<<<<<<<<< add your branch here.
pr:
- master
```
this will trigger the pipeline on every commits on that branch


### When pipeline fails

Github will display
kontainapp.km  Failing after ...  Details

Click on 'Details' then click on 'Errors' to get to the azure devops build detailed logs.

if Waifor completion or timeout task fails you want to look at 'get logs' task details.


### Skip CI for individual commits

include [skip ci] in the commit message or description of the HEAD commit and Azure Pipelines will skip running CI.

