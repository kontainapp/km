#!/bin/bash

make clean
make -C container-runtime clobber
make -j
make -C tests testenv-image
make -C tests test