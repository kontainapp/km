Kontaind is a daemon set running on all nodes that are running kontain. Currently, there are two components:
1. A installer to set up the node, running as a initContainer.
2. A k8s device plugin that provide /dev/kvm device to the containers.

To run:
```bash
make -C installer push-runenv-image
make install
make test
```