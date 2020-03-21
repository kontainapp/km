# Conversion to Kontainers

Automation to convert Nokia Java containers to the ones running the payloads in Kontain VM with Java.km unikernel.
Use `make help` for help

Typical sequence of commands:

```bash
make login # login to Azure and Azure private registry
make pull # One time, for pulling and tagging orginal images. Assumes login.
make all  # same as 'make' - builds kontain/nokia-* images from the base ones
make test # validate Kontainer with zookeeper .... just make sure it starts to listen
```

## A few tricks

To generate Kontainer with extra KM flags, use `make clean all KMFLAGS="..."` (`clean` is needed since Makefile does not know the flags have changed). E.g. `make clean all KMFLAGS='-V(mem|hc) --core-on-err'`

To start zookeeper Kontainer with bash so you can navigate
`make test DOCKER_TI="-ti" ARGS=bash`. To kill the test, ^Z and then 'kill -9 %%'

To clean up Kafka disks and kill Nokia test containers `make clean-disks`. Note that this target may reporte container names errors, this just indicates that these containers are already gone, and can be ignored.

## Running tests

After `make flatten`, you will have 3 types of Docker images:

* original Nokia ones, i.e `kontainstage.azurecr.io/nokia/ckaf/kafka`
* mutilayer Kontainer image (with KM inside), i.e. `kontain/nokia/kafka`
* flattened out Kontainer image (with KM inside), i.e. `flat-kontain/nokia/kafka`

The Kontainer multilayer and flat are functionally identical, but differ in size.

To run test, you may want to run `make clean-disks` to start from clean Kafka/Zookeeper DB.

Then, in ../kafka-test-tools/bin, run `./startTest.sh test-case-ckaf-01`.

### Config

We did a few changes to these script to control which image to use (original or Kontainer), how long to sleep before starts, and if it is needed to remove containers after runs.

Since entrypoints and behavior of Containers and Kontainers is the same, we use Nokia script essentially not modified (with the exception of the config stuff above)

We use environment vars to control the following:

### CPREFIX

Controls which docker image is picked up by the test

* By default it uses `kontainstage.azurecr.io/nokia/ckaf` value so it will use regular Java containers.
* If you `setenv CPREFIX=kontain/nokia`, it will run multilayer Kontainer.
* If you `setenv CPREFIX=flat-kontain/nokia`, it will run flattened out Kontainer.

### RM_FLAG

Nokia test  script will try to kill all containers after a run. This will delete all container logs and all related info (e.g. info about crashes :-)) . Pass environment variable `RM_FLAG="--label whatever"` to cancel container removal at the end of the run. Basically, this will replace "--rm" flag with a flag `--label` to docker that does not do much.

### SLEEP

The script stops for 10 sec after running each container. Environment SLEEP is number of seconds, you can set it to change the default. `setenv SLEEP=5`

### NOFILES

For some reason, Nokia script use *--nofile=122880:122880* to set files rlimit in the container. It wastes memory,  and actually useless in java.km. You may change it, to say 1024 with `setenv NOFILE=1024:1024`

*** END OF DOCUMENT ***
