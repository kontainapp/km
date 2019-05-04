#!/bin/bash

git clone https://github.com/IAIK/meltdown

(cd meltdown; patch -p1 < ../meltdown.patch; make)
