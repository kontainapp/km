FROM scratch

# turn off km symlink trick and minimal shell interpretation
ENV KM_DO_SHELL NO
ADD --chown=0:0 busybox/_install /
# stopgap - krun will insert this at the beginning, put it here for now
#ENTRYPOINT [ "/opt/kontain/bin/km" ]
