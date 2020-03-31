ARG runenv_image_version=latest

FROM kontain/runenv-dweb:${runenv_image_version}

EXPOSE 8080
ENTRYPOINT [ "/opt/kontain/bin/km", "dweb.km" ]
CMD [ "8080" ]