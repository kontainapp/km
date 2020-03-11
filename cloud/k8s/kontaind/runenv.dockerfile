FROM busybox
COPY . /kontain
CMD [ "rm -rf /opt/kontain; cp -r /kontain /opt/kontain" ]