
# Known Issues and Limitations 
This represents a select set of current known issues. For a complete list, refer to the "Known Issues" Milestone in the `kontainapp/km` repo. 
## Language systems and libraries
*   CPU affinity API is silently ignored
*   Some of the huge ML packages (e.g. TensorFlow) are not fully tested with `Python.km`
*   Native binaries (with glibc) are experimental; although we test them in the CI, these may have issues.  
*   Kontain uses the musl implementation of the standard C library. Kontain currently does not fully support the glibc implementation. As a result, dynamically or statically linked executables using glibc, when run as a unikernel in Kontain VM, can cause a workload core dump. 
*   Language runtime base images are provided for a single version per language, e.g., for Python 3.7 and not for Python 2 or Python 3.8.
## Kontain Monitor 
*   There is no management plane to enumerate all running Kontain VMs
*   Using snapshots: 
    *   Floating point status is not retained across snapshots
    *   Snapshots are per VM. Coordinated snapshots for parent + children are currently not supported.
*   Only a subset of `/proc/self` is implemented (the parts we see being used in Node.js, Python 3 and JVM 11)
*   `getrlimit` and `setrlimit` are not virtualized and are redirected to the host
## Debugging
*   Stack trace through a signal handler is not useful
*   _Handle_ variables in thread local storage: currently ‘p var’ will generate an error
*   Floating point registers are not supported
*   GDB follow-fork mode cannot be used to follow the child process after a fork. To enable debugging of a child process, you can add a variable to the parent KM environment. For more information, see the “[Debugging Child Processes and exec Workloads](debugging-guide.md#debugging-child-processes-and-exec-workloads)" section in [*Debugging Kontain Unikernels*](debugging-guide.md). 
*   KM GDB server testing has been done using a GDB client with version GNU gdb (GDB) Fedora 9.1-5.fc32.
NOTE: For a detailed guide to Kontain debugging, refer to this document: [*Debugging Kontain Unikernels*](debugging-guide.md).
## Kontain Vagrant Box 
*   At installation, KKM, the kernel virtualization module, is tightly coupled with the host kernel version. If the host kernel is upgraded, the new kernel will not have KKM, and Kontain will not work. For now, we recommend against upgrading the kernel.
## Docker and Kubernetes
*   When Docker is installed on Ubuntu using `snap`, the location of the config file and the name of the service are different. You can consult Snap and Docker documentation for correct location and names, or you can remove the Snap version and re-install Docker using `apt-get`.
*   When running on Kubernetes, Kontain uses a [third-party KVM device plug-in](https://github.com/kubevirt/kubernetes-device-plugins/blob/master/docs/README.kvm.md) to provide unprivileged pods access to `/dev/kvm`. On rare occasions, this plug-in refuses access to `/dev/kvm`. Workaround: Re-deploy _kontaind_.   
*   Until `krun` invocation is supported on Kubernetes, you will need to configure the YAML to invoke KM. This workaround is documented under “[Invoking Kontain from Kubernetes](user-guide.md#invoking-kontain-from-kubernetes)” in the [*Kontain User Guide*](user-guide.md).
## ‘Unimplemented Hypercall’ Error
When running an unmodified Linux executable as a unikernel, Kontain will automatically load the required support into the Kontain VM. This code converts syscalls from the app to hypercalls Kontain VM can handle.
If the application code attempts to use an unsupported system call, KM will report an ‘Unimplemented hypercall’ error and the workload will abort. 
If you get this error, let us know and we will work with you to resolve it. NOTE: In our testing to date, with applications using supported interpreted languages and common tools (including SpringBoot in Java, Django in Python, and TensorFlow), we have not run into this issue. 

