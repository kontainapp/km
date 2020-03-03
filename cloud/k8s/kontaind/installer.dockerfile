FROM busybox
LABEL Vendor="Kontain.app" Version="0.1"
COPY km /kontain/runtime/km
ENTRYPOINT [ "sh", "-c" ]
CMD [ "rm -rf /opt/kontain; cp -r /kontain /opt/kontain" ]