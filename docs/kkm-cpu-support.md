# KKM CPU support(this is a live document)

KKM is implemented to support KM on AWS isntances that do not support KVM.

## Instance support

KKM runs only on AWS HVM instances starting from gen2. We support instances with a minimum of 2 VCPUs and 2GB or memory. t2.medium is the oldest AWS instance supported by KM, KKM combination.

KKM implementation is very tightly coupled with processor architecture and Linux kernel.

## Linux support

KKM is at this time supported on 5.x x86_64 kernels. KKM requires Linux kernel to be complied with CONFIG_PAGE_TABLE_ISOLATION.

## Processor support

KKM uses certain CPU features. Some of these include

PCID and INVPCID

XSAVES or XSAVE

AVX

In absense of necessary features KKM driver will register in a limited function state. In limited function state only KKM_CPU_SUPPORTED ioctl may succeeed.

## How to check if kkm supports CPU

KKM driver at the time of init checks for necessary features.  When the necessary featuers are not available KKM driver logs a message. When CPU is not supported only KKM_CPU_SUPPORTED ioctl works. KKM_CPU_SUPPORTED ioctl will return enum kkm_cpu_supported.

This is a proprietary internal document. Not for consumption outside Kontain.