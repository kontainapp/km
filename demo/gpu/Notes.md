# Development notes for GPGPU support in KM

## Backgound

General Purpose GPU (GPGPU) is using GPU hardaware for general purpose computing. This is
in contrast to the more traditional graphics use cases.

There are competing standards for GPGPU programing frameworks, the most popular being CUDA, OpenCL,
and OpenACC. The most popular by far is CUDA, which is propietary to NVIDIA and only supports NVIDIA hardware.
OpenCL is the next most popular alternative and supports many types of GPU including those from AMD, Intel,
Sansung, and IBM as well as NVIDIA. OpenACC is also supports multiple GPU types. OpenACC differs from OpenCL
mainly in how the GPUs themselves are seen.  OpenCL explicitly controls the devices through API functions. OpenACC is
a directive-based extensions to programming languages.  

In all frameworks, units of GPU work called 'kernels' are written and compiled. Kernels are written in a dedicated
langauge, typicaly derived from C. For example, the following is the standard CUDA vector add example.

```
// CUDA
__global__ void vecAdd(double *a, double *b, double *c, int n)
{
    // Get our global thread ID
    int id = blockIdx.x*blockDim.x+threadIdx.x;
 
    // Make sure we do not go out of bounds
    if (id < n)
        c[id] = a[id] + b[id];
}
```

And this is the same thing in OpenCL.

```
// OpenCL
__kernel void vector_add(__global const int *A, __global const int *B, __global int *C) {
 
    // Get the index of the current element to be processed
    int i = get_global_id(0);
 
    // Do the operation
    C[i] = A[i] + B[i];
}
```

Kernels are compiled and delivered to GPU cores by the framework.

Even though CUDA is the defacto standard for GPGPU, the fact that it is proprietary to NVDIA and requires NVIDIA
hardware makes it inconvienent for Kontain's GPU investigation unless or until we've got something from NVIDIA. For
that reason, the rest of this document will talk about OpenCL.

## OpenCL

<Pretty rough from here out>

OpenCL supports multiple GPU devices including 

OpenCL device names:
* `/dev/dri*` - Intel
* '/dev/kfd` - AMD
* `/dev/nvidia*` - NVIDIA

## Terms

* OpenCL installable client decoder (ICD)
