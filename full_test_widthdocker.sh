#!/bin/bash

make clean
make -C container-runtime clobber
make -j withdocker
make -C tests testenv-image
make -C tests test-withdocker