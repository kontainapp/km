# python,km with flask and Faktory PoC

This dir contains misc. examples of Python Flask usage (quite simple for now) which work unchanged in python.km unikernel.

Also, a PoC of Faktory is using the above Flask example. Faktory converts docker container to Kontainer with same code running in Kontain unikernel. PoC is dealing with python.km only for now

## Status

A somewhate hacked together prototype. See Makefile and Dockerfile* for comments.

## Usage

`Make` is used only to simplify typing. Targets are not interdependent, so `make run` will *not* do `make build` first.
See `make help` for more info


## Issues

* Python.km versions: On Ubuntu (with python3.6) flask-km test fails with sre (regexp) version mismatch. It is likely due to mismatch of interpreter (3.7.4) and iubuntu libs (3.6.8). We need multiple version with python versioning scheme
* see multiple notes in faktory_stem.dockerfile, Makefile and scripts

## Rough design for further work

### requirements

* take original containers  (python only for now)
* generate Kontain docker container (and Kontain runk kontainer when supported) with neccessary files:
  * needed .PY files (.pyc can stay to accelerate start)
  * python.km with needed shared libs linked in, and of the correct version
  * python3 shebang with python versioning scheme and links
* validate the result is functioning @ sanity check
  * potential: use requirements.txt and pip3 commands in dockerfile for this
  * potential (later): automate loading and building the needed modules and then linking them into KM
* further optimization: only pickup needed modules, not all available
* further optimization: freeze python.km after all needed modules are loaded and create optimized kontainer with faster start

* the conversion code should NOT have any specifics about individual apps hacked in, instead it should be driven from the source docker image metadata and content

### Planned workflow

(as of 9/22/19, partially implemented)

* Get source metadata - 'image inspect' results, and 'history' results. (potential: some manual input here, e.g. app name if invoked via script)
* In a container made 'FROM' the the source analyze content of the source image: python path, content of the folders, analyze imports
   * export needed files, generate shebang files, config for proper python.km build (version shared libs, etc..), cmd/env/entrypoint for target kontainer
* build proper .km files (if needed)
* FROM kontainapp/km (for docker) or scratch (for runk) create kontainer image, using exported files data created above

pieces:

* python script for analyzing source and generating all needed info for Kontainer (this one runs in our container build 'FROM' source)
* python or bash script for collecting all metadata prior to run (this one runs on host)

Created: 9/20/2019