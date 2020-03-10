FROM busybox
COPY . /kontain
ENTRYPOINT [ "sh", "-c" ]
CMD [ "rm -rf /opt/kontain; cp -r /kontain /opt/kontain" ]