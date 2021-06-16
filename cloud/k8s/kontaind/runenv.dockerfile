FROM busybox
COPY bin /kontain/bin/
COPY installer.sh /installer.sh
ENTRYPOINT [ "sh", "-c" ]
CMD [ "/installer.sh" ]
