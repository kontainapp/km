# KM Payload Filesystem Support

## Container Background

One of the key features of containers is a virtual root filesystem and linux namespace for processes running within the container.  This virtual file system contains the static files required for the container to work. In container systems like Docker, the virtual file system is implemented by running the container's processes in a 'chroot' directory with a private Linux namespace using Linux facilities like `pivot_root(2)`, `unshare(2)`, and `clone(2)`.

Unfortunately, some of these facilities require `CAP_SYS_ADMIN` privileges. (That is why Docker needs to be run with elevated privileges).

## Idea

Like Containers, a KM payload image includes a virtual file system that contains the static files needed by the payload image. Rather than use privileged Linux facilities to virtualize the guest's execution environment, KM does the virtualization itself.

As a first cut KM uses pathname translation to virtualize the guest payload's file system environment. KM establishes a root directory for the payload and only allows the payload to see files in the root directory or below.a By default, KM sets the guest's root directory to KM's current directory and `km --rootdir=<dir>` allows the guest's root directory to be set explicitly.

## Details
* Pathname interpretation (open(2), creat(2), etc.) in KM
* KM returns info to guest to allow direct access to data inside guest (future).
* guest runtime changed to support access to file data in guest memory.

Idea: namei, directory ops, and allocation in KM. Read write in guest.
* Take over open() and close in musl and implement new hcalls to support open().
* Open returns info in hc_call structure:
* * guest fd number.
* * length of file
* * pointer to data

Write operations proxy to linux.

A Kontain Image File is a tarball with the following contents:
* A config file that describes the payload's execution environment.
* A directory that contains payload and associated files.
  * payload directory is the guest's root. Directory must be checked for symlinks with relative path names that violate virtual root.

 
## Appendix A: System Calls with Pathnames

* open(2) [2], creat(2) [85], openat(2) [257]
* stat(2) [4], lstat(2) [6], access(2) [21]
* execve(2) [59]
* truncate(2) [76]
* getcwd(2) [79]
* chdir(2) [80]
* renam(2)e [82]
* mkdir(2) [83], rmdir(2) [84], mkdirat(2) [258]
* link(2) [86], unlink(2) [87], linkat(2) [265], unlinkat(2) [263]
* symlink(2) [88], readlink(2) [89], symlinkat(2) [266], readlinkat(2) [267]
* chmod(2) [90]
* chown(2) [92], lchown(2) [94]
* mknod(2) [133], mknodat(2) [259]
* statfs(s) [137]
* pivot_root(2) [155], chroot(2) [161]
* mount(2) [165], umount2(2) [166]
* swapon(2) [167], swapoff(2) [168]
* setxattr(2) [188], lsetxattr(2) [189]
* getxattr(2) [191], lgetxattr(2) [192]
* listxattr(2) [194], llistxattr(2)[195]
* removexattr(2) [197] lremovexattr(2) [198]
* mqopen(2) [240], mq_unlink(2) [241]
* inotify_add_watch(2) [254]
* unshare(2) [272]
* name_to_handle_at(2) [303]
* ftok(3) (SysV IPC)