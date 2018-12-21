# dockerfile for builds
# Usage will be 'docker run <container> make <target> - see ../../Makefile

FROM fedora:29

LABEL version="1.0" maintainer="Mark Sterin <msterin@kontain.app>"

RUN dnf install -y gcc \
		elfutils-libelf-devel elfutils-libelf-devel-static \
		make \
	&& dnf clean all

