# Containerd Shim for KRUN

This directory provides a shim to integrate KRUN with containerd.

This shim is derived from the standard RUNC shim that is part of containerd. Since
GO doesn't have inheritance, our SHIM implements the a composintion on top on the containerd
runc shim. Thw only difference is we inject a configuration option at task create time to 
force KRIN to be used as the runtime instead of RUNC.


Some helpful links:
- https://iximiuz.com/en/posts/implementing-container-runtime-shim/ Overview of `containerd`/shim/runc interactions.
- https://github.com/containerd/containerd/blob/v1.5.5/runtime/v2/README.md Shim authoring
- https://github.com/containerd/containerd/blob/main/docs/ops.md
- https://github.com/containerd/containerd/blob/main/runtime/v2/README.md
