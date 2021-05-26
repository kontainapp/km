FROM scratch

# turn off km symlink trick and minimal shell interpretation
ENV KM_DO_SHELL NO
ADD --chown=0:0 busybox/_install /
ADD km /