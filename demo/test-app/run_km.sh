#!/bin/bash
date +%s%N >& /tmp/start_time; /opt/kontain/bin/km --mgtpipe /tmp/km.sock /usr/bin/python3.8.km app.py
