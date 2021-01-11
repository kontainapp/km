#!/bin/bash
#
# Switch python between km and native

cd /usr/bin
rm -f python3.8

if [ "$1" == "km" ] ; then
   ln -s /opt/kontain/bin/km python3.8
else
   ln -s python3.8.orig python3.8
fi
