export LC_TIME=POSIX
export LC_ALL=C && export S_TIME_FORMAT=ISO
/usr/bin/env LC_TIME=POSIX sar -A 5 10000000 >  $1


