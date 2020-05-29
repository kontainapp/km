FROM busybox
COPY kontain /kontain
COPY installer.sh /installer.sh
ENTRYPOINT [ "sh", "-c" ]
CMD [ "/installer.sh" ]
