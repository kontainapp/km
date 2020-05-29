# Kontaind install and validation

Kontaind is a daemon set running on all nodes that are running kontain. Currently, there are two components:

1. A installer to set up the node, running as a initContainer.
2. A k8s device plugin that provides /dev/kvm device to the containers.

To run:
```bash
# Build and push image for initContaint to either Azure ACR or docker.io.
# For Azure/ACR, make sure you are logged in
# For public docker.io/kontainapp, use CLOUD=dockerhub
make runenv-image push-runenv-image
make deploy  # deploys daemon-set with initContainer
make test    # Validates demo-dweb deployment. Assumes demo-dweb runenv-image was pushed to dockerhub
```

## Test notes

test simply makes sure 'km dweb' starts and exits properly. It uses `docker.io/kontainapp/runenv-dweb:latest` so the test needs to be pushed with `CLOUD=dockerhub make -C payloads/demo-dweb runenv-image push-runenv-image`

