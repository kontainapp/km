# Supporting Kontain CRI Runtime on AWS EKS and ECS

While both the AWS Elastic Container Service (ECS) and Elastic Kubernetes Service (EKS) provide optimized
AMI's that are used by default, both products allow the user to define custom AMI's for their work. The
packer-based build receipes for Amazon's default AMI's are open-source and available via github.

* https://github.com/aws/amazon-ecs-ami
* https://github.com/awslabs/amazon-eks-ami

The strategy for integrating the Kontain runtime into ECS and EKS is to build custom AMI's that contain the
KKM driver and the Kontain runtime software. These new custom AMI's are built using the Amazon provided
packer recepies modified to include the Kontain runtime. The following are the forked github repos where
our changes to the recepies are kept:

* https://github.com/kontainapp/amazon-ecs-ami - deafult branch `kontain`
* https://github.com/kontainapp/amazon-eks-ami - default branch `kontain`

The strategy for re-syncing with upstream is the same as `krun`. The `master` branch is literally in sync
with the upstream repo and our changes are made to the `kontain` branch. To bring our branch up to date 
WRT upstream, we fetch the upstream changes to `master` and rebase the changes.

In both cases, Kontain's changes will:

* Install KKM driver
* Install KM, KRUN, and (optionally) the containerd shim.
* Configure the container manager in the AMI to use the Kontain runtime.

## ECS Details

* https://aws.amazon.com/blogs/containers/creating-custom-amazon-machine-images-with-the-ecs-optimized-ami-build-recipes/

<< To fill in >>

## EKS Details

Note: Nodes that are not part of an Amazon EKS Managed Node Group are not shown in the AWS console.

1. Create Cluster with no workers

```
$ eksctl create cluster --name <cluster name> --version 1.21 --without-nodegroup --region <region-name>
```

Hint: `aws eks --region <region-name> update-kubeconfig --name <cluster-name>` to setup local kubectl to talk to this cluster.

2. Create nodegroup that uses custom AMI.
```
eksctl create nodegroup --region <region-name> --cluster <cluster-name> --name <group-name>--nodes 1 --nodes-min 1 --nodes-max 1 \
    --ssh-access --ssh-public-key <ssh-public-key> --node-ami <custom-ami-name> --managed=false
```
