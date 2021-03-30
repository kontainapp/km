#!/bin/bash
date +%s%N >& /tmp/start_time; /usr/bin/python3.8.orig app.py
