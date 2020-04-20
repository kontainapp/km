FROM fedora:31

# Contains the scripts needs to run tests.
COPY scripts ./scripts
COPY km /opt/kontain/bin/km
COPY libc.so /opt/kontain/runtime/