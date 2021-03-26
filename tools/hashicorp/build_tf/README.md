# Custom built tensorflow

We require our own custom built version of tensorflow.
The code isn't changed, we just compile it with `-D_GTHREAD_USE_RECURSIVE_MUTEX_INIT_FUNC=1`
to disable use of non-POSIX primitives that are not supported on musl.

Run `vagrant up --provision`, it takes 2 - 3 hours.
Tensorflow will be built in the virtual machine,
then the resulting tensorflow<something>.whl file will be copied into that directory.

Instead of building it locally it could be downloaded from azure.
Simply search for `tensorflow-2.4.1-cp38-cp38-linux_x86_64.whl` in azure panel,
ythen download it here.

TODO: Make it through cli and automate it.

