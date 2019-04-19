# Kontain Payloads

This directory keeps a collection of payloads we explicitly converted to work as KM payloads.
In the future we will also place here the code to auto-convert payloads

* Payloads can be built with `make all` (does initial pull , configure and build) or  `make build` (this one will force some reconfig and rebuild of the payloads).
* Payloads are distributed as Docker images and can be created with `make distro`  - though we recommend doing `make distro` from top level , so KM base Docker image gets built first.
* `make publish` will push them to Azure ACT (assuming you've loggged in)
* `make distroclean` and 'make publishclean` are cleaning the artifacts

All 'make' commands (as always) can be also entered while in individual payload directory
