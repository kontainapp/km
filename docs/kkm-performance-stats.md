# KVM driver performance on different platforms.


| platform | kontain emulator | Hypercall | Read Page fault | Write Page fault
| :--:     |:--:              |        --:|              --:|              --:
| Physical | syscall | 0.495 | 0.937 | 1.544
| Physical | kvm hcall | 3.183 | 1.883 | 1.815
| Physical | kkm hcall | 2.214 | 2.172 | 3.591
| L1 on Physical | syscall | 0.531 | 1.029 | 2.648
| L1 on Physical | kvm hcall | 33.723 | 39.460 | 39.456
| L1 on Physical | kkm hcall |  15.861 | 16.059 | 18.514
| L1 on azure | syscall | 0.308 | 0.009 | 1.186
| L1 on azure | kvm hcall | 23.040 | 0.054 | 1.237
| L1 on azure | kkm hcall | 7.389 | 0.023 | 1.216
| L1 on aws | syscall| 0.369 | 0.894 | 2.049
| L1 on aws | kvm hcall | NA | NA | NA
| L1 on aws | kkm hcall | 5.493 | 5.577 | 7.657


## Test description.
+ Hypercall
	* Time taken for 1 millions dummy system call's.
+ Read Page fault
	* Time taken for 1 millions read page faults. Page is readable after this.
+ Write Page fault
	* Time taken for 1 millions write page faults. Page is writable after this.

## Platform specification.
### Physical hw
* Intel(R) Core(TM) i5-6400 CPU @ 2.70GHz.
* 16GB ram.
* Fedora 32
* Linux kernel 5.7.7

### L1 on Physical VM
* Intel(R) Core(TM) i5-6400 CPU @ 2.70GHz.
* 8GB ram.
* Fedora 32
* Linux kernel 5.7.7

### L1 on AZURE
* Intel(R) Xeon(R) Platinum 8171M CPU @ 2.60GHz
* 8GB ram.
* Ubuntu 18.04
* Linux kernel 5.3.0

### L1 on AWS
* Intel(R) Xeon(R) Platinum 8124M CPU @ 3.00GHz
* 8GB ram.
* Fedora 32
* Linux kernel 5.6.14
