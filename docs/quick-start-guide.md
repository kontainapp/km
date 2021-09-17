# Kontain Quick Start Guide

Kontain is the way to run container workloads "secure, fast and small—choose three."
Kontain runs workloads with virtual machine level isolation and security,
but without traditional VM overhead.
In fact, Kontain generates smaller artifacts and much faster startup times than "classic" Docker containers.
Kontain works by wrapping your code into a unikernel executable that is packaged as a standard container.
To distinguish a Kontain container from a "classic" container, we spell it _kontainer_.

Use cases for Kontain include ML inferencing, serverless/FaaS computing,
mobile edge cloud, healthcare, financial services, and ecommerce.
If you use containers for your application,
but are sensitive to security, performance, scalability, and the cost of running your application in production,
Kontain may be for you.

## Installing Kontain

Kontain runs on Linux kernel version 4.15 or newer,
running on Intel VT (vmx) or AMD (svm) with virtualization enabled.
Recommended distros are Ubuntu 20.04 and Fedora 32, or newer.

To install [<sup>1</sup>](#1) the latest Kontain release in a Linux system, run:
```bash
curl -s https://raw.githubusercontent.com/kontainapp/km/latest/km-releases/kontain-install.sh | sudo bash
```

To install a release other than _latest_, for example _v0.9.0_, the URL is:
```bash
https://raw.githubusercontent.com/kontainapp/km/v0.9.0/km-releases/kontain-install.sh
```

This installs the necessary files in your `/opt/kontain` directory and configures the Kontain runtime for docker and podman.
It also executes a smoke test of the unikernel virtual machine.

### MacOS or Windows machine

Currently, the only way run Kontain on MacOS or Windows is to run it in a Linux VM[<sup>2</sup>](#2).
To simplify running Kontain we provide pre-configured VMs available from Vagrant Cloud:

* Ubuntu 20.10 – https://app.vagrantup.com/kontain/boxes/ubuntu2010-kkm-beta3
* Fedora 32 – https://app.vagrantup.com/kontain/boxes/fedora32-kkm-beta3

You’ll need Vagrant ([https://www.vagrantup.com](https://www.vagrantup.com))
and VirtualBox ([https://www.VirtualBox.org](https://www.VirtualBox.org)) installed on your machine.

Once the VM is up (`vagrant init kontain/ubuntu2010-kkm-beta3; vagrant up`) and logged in (`vagrant ssh`),
you have Kontain installed and configured.

## Starting with Kontain

To run a simple Kontain unikernel manually:
```bash
    $ /opt/kontain/bin/km /opt/kontain/tests/hello_test.km Hello, Kontain!
    Hello, world
    Hello, argv[0] = '/opt/kontain/tests/hello_test.km'
    Hello, argv[1] = 'Hello,'
    Hello, argv[2] = 'Kontain!'
```

_km_ is the Kontain virtual machine monitor.
_hello_test.km_ is a very simple unikernel that prints out its arguments then exits.

To wrap this simple unikernel in a standard container format we need this Dockerfile
```dockerfile
    FROM scratch
    ADD hello_test.km /
    ENTRYPOINT ["/hello_test.km"]
    CMD ["from", "docker"]
```

Put this Dockerfile and `hello_test.km` in an empty directory and run:
```bash
    docker build -t try-kontain .
```

Now you can run your kontainer from the docker (or podman) command line using the Kontain runtime:
```bash
    $ docker run --runtime=krun --rm try-kontain:latest
    Hello, world
    Hello, argv[0] = '/hello_test.km'
    Hello, argv[1] = 'from'
    Hello, argv[2] = 'docker'
```
_krun_ is the Kontain runtime.

### Native Linux executables

Static native Alpine Linux executable can run under Kontain, unmodified, as a unikernel.
Same applies to other static executables build with musl libc.

## Using Kontain base images

Kontain provides a collection of language interpreters packaged as unikernels on Docker Hub.
Let’s use one of them to create a python kontainer.

You can use the provided `/opt/kontain/examples/python/micro_srv.py` example, or your own program.
`micro_srv.py` implements a RESTful API endpoint.
The dockerfile looks like this:
```dockerfile
    FROM kontainapp/runenv-python
    ADD micro_srv.py /
    EXPOSE 8080
    ENTRYPOINT ["/usr/local/bin/python3","/micro_srv.py","8080"]
```

Put this dockerfile and `micro_srv.py` in an empty directory and run:
```bash
    docker build -t try-kontain .
```

Run the kontainer with `docker run --runtime=krun -p 8080:8080 --rm try-kontain:latest` then access the API with curl:
```bash
    $ curl -s http://localhost:8080 | jq .
    {
      "sysname": "Linux",
      "nodename": "807c605eca29",
      "release": "5.11.22-100.fc32.x86_64.kontain.KVM",
      "version": "#1 SMP Wed May 19 18:58:25 UTC 2021",
      "machine": "x86_64",
      "received": "ok"
    }
```

The process is similar with Node.js, or Java code.  Just use the appropriate kontainapp base images –
`kontainapp/runenv-node` or `kontainapp/runenv-jdk-11`.

## Building kontainers from compiled code

For compiled languages such as C and Go, building a kontainer involves re-linking your compiled code.

For example, let's build the web server https://github.com/davidsblog/dweb (it's written in C) into a kontainer.  Begin by using standard `make`:
```bash
$ git clone https://github.com/davidsblog/dweb.git
$ cd dweb/dweb
$ make
gcc -o dweb dweb.c dwebsvr.c -pthread -std=gnu99
```

Note the action `make` has done.
We are going do the same, but with a Kontain tool:
```bash
$ /opt/kontain/bin/kontain-gcc -o dweb dweb.c dwebsvr.c -pthread -std=gnu99
```

Now we need a dockerfile:
```dockerfile
    FROM scratch
    ADD . /app/
    WORKDIR /app
    ENTRYPOINT ["/app/dweb"]
    CMD ["8080"]
```

To build and run the kontainer:
```bash
$ docker build -t kontain-dweb .
$ docker run -p 8080:8080 --runtime=krun --rm -i xxx
```

You can interact with the web server via `curl http://localhot:8080` or pointing a web browser to the same address.

## Notes

<a class="anchor" id="1">1.</a>
To uninstall Kontain simply run:
```
    sudo bash /opt/kontain/bin/kontain-install.sh -u
```
<a class="anchor" id="2">2.</a>
We are working on a more seamless way to run Kontain on MacOS and Windows.
Stay tuned.