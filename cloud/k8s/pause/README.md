This is the pause container used as the pod infra container in K8s.

# Why do we need this?

To support K8s workflow, pause container, which serves as the infra container
for a pod, needs to be packaged with kontain.

# To build

To build the infra container, run: `make distro`.