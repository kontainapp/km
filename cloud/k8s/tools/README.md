# Tools for Kubernetes

This directory contains tools to diagnose and debug kubernetes clusters.

## hacknode.yaml

This creates a privileged DaemonSet in the `kube-system` namespace with a base name of `kontain-node-hack`
to run on all Nodes. The daemon set mounts the Node's root file system `/` as `/host-root` and sleeps forever.

The pod(s) running the DamonSet are found with `kubectl get pod -A -o wide`. A shell can them be started
on a DamonSet pod with `kubectl exec -it`.

### Tips and Tricks

- Once logged into the DaemonSet pod, the Nodes `/proc` is mounted as `/host-root/proc`, so the command `chroot /host-root ps -aef` will show all processes running on the Node.
