#!/bin/bash

[ "$TRACE" ] && set -x

KM_TOP=$(git rev-parse --show-toplevel)

${KM_TOP}/build/km/km ./server.km > /dev/null 2>&1 &

sleep 0.3

for i in $(seq 3) ; do
   curl --silent http://127.0.0.1:8080 | grep -q 'Hello World!'
   if [ $? != 0 ]; then
      kill -KILL %%
      exit
   fi
done

kill -KILL %%

