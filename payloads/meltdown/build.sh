#!/bin/bash

if [ ! -d meltdown ] ; then 
  git clone https://github.com/IAIK/meltdown
  (cd meltdown; patch -p1 < ../meltdown.patch)
fi
make -C meltdown
