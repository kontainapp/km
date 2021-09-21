
# Known Issues and Limitations

## Language systems and libraries
* CPU affinity API is silently ignored
* Language runtime base images are provided for a single version per language,
  e.g. for Python 3.8 and not for Python 2 or Python 3.9
## Kontain Monitor
* There is no management plane to enumerate all running Kontain VMs
* Snapshots are per VM. Coordinated snapshots for parent + children are currently not supported
* Only a subset of `/proc/self` is implemented (the parts we see being used in Node.js, Python 3 and JVM 11)
* `getrlimit` and `setrlimit` are not virtualized and are redirected to the host
## Debugging
* Stack trace through a signal handler is not useful
* Attempt to access variables in thread local storage `p var` fails
* Floating point registers are not supported
* GDB _follow-fork_ mode cannot be used to follow the child process after a fork
  For more information, see the
  “[Debugging Child Processes and exec Workloads](debugging-guide.md#debugging-child-processes-and-exec-workloads)"
* KM GDB server testing has been done using a GDB client with version GNU gdb (GDB) Fedora 9.1-5.fc32

For a detailed guide to Kontain debugging, refer to this document:
[*Debugging Kontain Unikernels*](debugging-guide.md).
## Docker and Kubernetes
* When Docker is installed on Ubuntu using `snap`, the location of the config file and the name of the service are different. You can consult Snap and Docker documentation for correct location and names, or you can remove the Snap version and re-install Docker using `apt-get`
* Kontain doesn't support daemon set installation for containerd based clusters.
  Only CRI-O is supported
## ‘Unimplemented Hypercall’ Error
If the application code attempts to use an unsupported system call,
KM will report an ‘Unimplemented hypercall’ error and the workload will abort.
If you get this error, let us know and we will work with you to resolve it.
In our testing to date, with applications using supported interpreted languages and common tools
(including SpringBoot in Java, Django in Python, and TensorFlow),
we have not run into this issue.

