# Supporting Kontain CRI Runtime on AWS EKS and ECS

While both the AWS Elastic Container Service (ECS) and Elastic Kubernetes Service (EKS) provide optimized
AMI's that are used by default, both products allow the user to define custom AMI's for their work. The
packer-based build recipes for Amazon's default AMI's are open-source and available via github.

* https://github.com/aws/amazon-ecs-ami
* https://github.com/awslabs/amazon-eks-ami

The strategy for integrating the Kontain runtime into ECS and EKS is to build custom AMI's that contain the
KKM driver and the Kontain runtime software. These new custom AMI's are built using the Amazon provided
packer recipies modified to include the Kontain runtime. The following are the forked github repos where
our changes to the recipies are kept:

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

ECS allows work ('tasks' in ESC termanology) to run in Fargate, EC2 instances, and external servers. Fargate is
a propietary, fully managed container executon system where AWS controls the OS and the node type. With EC2 instances and
external servers, the user controls the execution environment.

In principal, Kontain can support EC2 instances and external servers in an ECS cluster. AWS has documentation on how to do this.

An inconvienent issue for us is ECS does not support choosing a container runtime when a task is submitted. All tasks scheduled
to a particular node (EC2 instaance or external server) use the default runtime specified in `/etc/docker/daemon.json`.
This article talks about experiences supporing gVisor within ECS and contains valuable information for this effort:
https://itnext.io/gvisor-on-ecs-78d4edc24604.

As an intial offering for ECS we will create a custom AMI based with KM and KKM pre-installed. The AMI will be based on the
standard ECS AMI and is built using the recipies provided by https://github.com/aws/amazon-ecs-ami. We've created a fork
called https://github.com/kontainapp/amazon-ecs-ami and we will make our changes to the `kontain` branch in the fork.

The ECS AMI recipies support multiple configurations:
* al1 - Amazon Linux 1 for i386/x86 (old)
* al2 - Amazon Linux 2 for i386/x86 (current)
* al2arm - Amazon Linux 2 for ARM (current)
* al2gpu - Amazon Linux 2 for i386/x86 plus NVIDIA GPU (current)
* al2inf - Amazon Linux 2 for i386/x86 plus Inferentia (current)

Initially, we are only building the `al2` variation. We can do others if/when demand requires.

* https://aws.amazon.com/blogs/containers/creating-custom-amazon-machine-images-with-the-ecs-optimized-ami-build-recipes/
* https://docs.aws.amazon.com/AmazonECS/latest/developerguide/ECS_CLI_installation.html

## EKS Details

Note: Nodes that are not part of an Amazon EKS Managed Node Group are not shown in the AWS console.

These instructions use the `eksctl` command. See https://eksctl.io/.

1. Create Cluster with no workers

```
$ eksctl create cluster --name <cluster name> --version 1.21 --without-nodegroup --region <region-name>
```

Hint: `aws eks --region <region-name> update-kubeconfig --name <cluster-name>` to setup local kubectl to talk to this cluster.

2. Create nodegroup that uses custom AMI.
```
eksctl create nodegroup --region <region-name> --cluster <cluster-name> --name <group-name> --nodes 1 --nodes-min 1 --nodes-max 1 \
    --ssh-access --ssh-public-key <ssh-public-key> --node-ami <custom-ami-name> --managed=false
```
