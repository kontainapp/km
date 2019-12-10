#!/bin/bash

src=busybox
if [ -v BASH_TRACING ] ; then set -x ; fi
set -e

if [ ! -d $src ]; then
    git clone https://github.com/mirror/busybox.git $src
fi

cp config.txt $src/.config
make -C $src -j16
cp $src/busybox $src/busybox.km
