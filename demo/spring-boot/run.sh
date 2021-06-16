#!/bin/sh

rm -rf /tmp/km.sock
date +%s%N >& /tmp/start_time
/opt/kontain/bin/km --mgtpipe /tmp/km.sock /opt/kontain/java/bin/java.kmd -XX:-UseCompressedOops -jar /app.jar
