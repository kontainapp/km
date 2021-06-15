#!/bin/sh

date +%s%N >& /tmp/start_time
/opt/kontain/bin/km kmsnap
