# Testbed for containerd

## Building runc

- Clone runc from github: `git clone git@github.com:opencontainers/runc.git`
- Make a version: `cd runc; git checkout v1.0.2; make`
- Binrary is `runc`

## Building containerd

- Clone containerd from github: `git clone git@github.com:containerd/containerd.git`
- Make a version: `cd containerd; git checkout v1.5.5; make`
- Binaries in `bin/`

## Building and test a contatainerd testbed

Copy the `bin/` directory from container to this directory. Copy `runc` binary into `bin/` directory. 
Then run:
- `$ docker build -t test-containerd -f Dockerfile .` # to create a container with containerd in it
- `$ docker run -it --rm --privileged test-containerd bash` # to start the container.
- `# containerd &` # Inside the container
- `# ctr image pull docker.io/library/hello-world:latest`
- `# ctr run --snapshotter=native --tty docker.io/library/hello-world:latest demo`

You shoud see at least the following. You may also see other messages from `containerd`.

```
Hello from Docker!
This message shows that your installation appears to be working correctly.

To generate this message, Docker took the following steps:
 1. The Docker client contacted the Docker daemon.
 2. The Docker daemon pulled the "hello-world" image from the Docker Hub.
    (amd64)
 3. The Docker daemon created a new container from that image which runs the
    executable that produces the output you are currently reading.
 4. The Docker daemon streamed that output to the Docker client, which sent it
    to your terminal.

To try something more ambitious, you can run an Ubuntu container with:
 $ docker run -it ubuntu bash

Share images, automate workflows, and more with a free Docker ID:
 https://hub.docker.com/

For more examples and ideas, visit:
 https://docs.docker.com/get-started/
```

Note:

The `ctr run --snapshotter=native --tty docker.io/library/hello-world:latest demo` is shorthand for:
- `ctr container create --snapshotter=native docker.io/library/hello-world:latest demo2`
- `ctr task start demo2`

## containterd logging levels

The containerd `-l` flag controls the log level.  Starting with `containerd -l trace`
gives all tracing messages (which isn't too bad).

For all containerd flags run `containerd --help`.

Tips:

You can change the code in containerd, remake, and rebuild your container. Some useful things to insert are
`debug.PrintStack()` and containerd debug log statements (which you'll need to start containerd with `-l debug`
or `-l trace` to see).

## Things to do with containerd

the `ctr` program is the main tool to interact with `containerd`. Some things you can do with it:
- Pull image `ctr image pull docker.io/library/hello-world:latest`
- Create container `ctr container create --snapshotter=native docker.io/library/hello-world:latest demo`

Note:
That's as far as I've gotten. I'm not sure why I need `--shapshotter=native` but the default `overlay` 
snapshotter doesn't work. Sometime to look into.
