FROM scratch

# turn off km symlink trick abd minimal shell interpretation
ENV KM_DO_SHELL NO
ENTRYPOINT ["/opt/kontain/bin/km"]
ADD --chown=0:0 busybox/_install /
