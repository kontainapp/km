This is an enhancement to KRUN to optionally trigger KM with a docker label/OCI annotation.

## Background

KRUN is derived from CRUN and Kontain-specific logic is triggered based on `argv[0]` (the name of the executable). If name is 'krun` then kontain specific logic is run to mount the virtualization device (kvm or kkm) in the container and to start the entrypoint under the control of km. Otherwise, the container is started as normal.

This is aligned with standard OCI based orchestration systems like kubernetes as well as with container systems that support multiple user settable runtimes like docker and podman.

This doesn't work so well with AWS ECS however.

## The Problem with AWS ECS

ECS does not expose multiple user settable runtime. It's model is each node has a single type of runtime and all containers that run on that node run under that runtime, including a special Amazon provided `ecs-agent` container.

ECS deals in docker images. Docker images can contain labels of the form `<key>=<value>` and an ECS container inherits the labels the its parent image.

## Proposal

Kontain payload images are identified with the label `app.kontain.version=1`. The label is introduced in `payloads/busybox/runenv.dockerfile` and `payloads/dynamic-base-scratch/runenv.dockerfile` so is therefore inherited by the rest of the of the kontain payload containers.

A new name of `krun`, `krun-label-trigger` will be recognized. When started as `krun-label-trigger` the decision to use KM or
not is based of the presense of the `app.kontain.version=1` label. If present, then the KM path is taken.
If not present, then the standard `crun` path is taken.

The `/etc/docker/daemon.json` file for ECS nodes is:
```
[root@ip-10-0-0-121 ~]# cat /etc/docker/daemon.json 
{
    "runtimes": {
        "krun": {
            "path": "/opt/kontain/bin/krun-label-trigger"
        }
    }
}
```
(Note: the name of the runtime is irrelevent. What's important is the name of the executable file.)
