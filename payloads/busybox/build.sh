#!/bin/bash
set -e ; [ "$TRACE" ] && set -x

src=busybox

if [ ! -d $src ]; then
    git clone https://github.com/mirror/busybox.git $src
fi

cp config.txt $src/.config
make -C $src -j16
make -C $src install
mv $src/_install/bin/busybox $src/_install/bin/busybox.km
ln -s /opt/kontain/bin/km $src/_install/bin/busybox
cp $src/busybox $src/busybox.km
