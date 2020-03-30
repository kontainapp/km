ARG runenv_image_version=latest

FROM kontain/runenv-dweb:${runenv_image_version}

EXPOSE 8080
CMD [ "8080" ]