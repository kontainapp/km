DTYPE ?= alpine

run:
	docker run --rm -p 8080:8080 flask:$(DTYPE)

build:
	m4 Dockerfile.$(DTYPE) | docker build -f - -t flask:$(DTYPE) .

build-all:
	for t in alpine fedora ubuntu ; do $(MAKE) MAKEFLAGS="$(MAKEFLAGS)" build DTYPE=$$t; done