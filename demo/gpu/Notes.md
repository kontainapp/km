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

OpenCL is a "open" standard for accessing the capabilities of off-board computing devices like GPUs. OpenCL consists
of a host API for controlling these devices, and one or more programming languages for writing device kernel functions.
A architectural desciption of OpenCL can be found here:
https://www.khronos.org/registry/OpenCL/specs/3.0-unified/html/OpenCL_API.html#_the_opencl_architecture

OpenCL was originally created by Apple, but was spun off to a non-profit called Khronos group. OpenCL 1.x
offred the host API's and a single kernel programming langauge based on C. OpenCL 1.x was picked up widely and
today OpenCL 1.2 is supported by most commercial GPU vendors including NVIDIA, AMD, and Intel.

OpenCL 2.x added features and support for a C++ based langauge for kernels. Adoption of OpenCL 2.x was spotty and
led to a lot of "OpenCL is dead" talk.

Recently OpenCL 3.0 was released. In OpenCL 3.0, everything in OpenCL 1.2 is mandatory but things from OpenCL 2.x
and any new OpenCL 3.x features are optional. Significantly, the OpenCL 2.x C++ kernel langauge is depricated and
a new C++ kernel language introduced.

Ironically, in the meantime Apple dropped support for OpenCL.

None of this means that OpenCL is dead. OpenCL 1.2 and the C based kernel langauge appears to be very much alive
as a cross-vendor interface for applications.

TODO:
* OpenCL Platform architecture where vendors can plug in their own (typically binary) implementation(s).
* Re-enforce need to support arbitrary binaries.

<Pretty rough from here out>

OpenCL supports multiple GPU devices including 

OpenCL device names:
* `/dev/dri*` - Intel
* '/dev/kfd` - AMD
* `/dev/nvidia*` - NVIDIA

## Terms

* OpenCL installable client decoder (ICD)
