#!/bin/sh

rm -f /tmp/km.sock
cd /tmp
echo > /mnt/start_time
KM_MGTPIPE=/tmp/km.sock python /home/appuser/app.py
