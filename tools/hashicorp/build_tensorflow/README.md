# Custom built tensorflow

We require our own custom built version of tensorflow.
The code isn't changed, we just compile it with `-D_GTHREAD_USE_RECURSIVE_MUTEX_INIT_FUNC=1`
to disable use of non-POSIX primitives that are not supported on musl.

Run `make`, it takes 2 - 3 hours.
Tensorflow will be built in the virtual machine,
then the resulting tensorflow<something>.whl file will be copied into this directory.

Instead of building it locally it could be downloaded from azure.
Run `make download` to do that, after logging in Azure.

To replace the version stored in azure, run `make upload`.
Need to be logged in Azure.

