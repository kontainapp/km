FROM busybox
LABEL Vendor="Kontain.app" Version="0.1"
COPY . /kontain
ENTRYPOINT [ "sh", "-c" ]
CMD [ "rm -rf /opt/kontain; cp -r /kontain /opt/kontain" ]