#!/bin/sh

rm -f /tmp/km.sock
echo > /mnt/start_time
KM_MGTPIPE=/tmp/km.sock /opt/kontain/java/bin/java -XX:-UseCompressedOops -jar /app.jar
