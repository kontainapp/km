ARG runenv_image_version=latest

FROM kontainapp/runenv-dweb:${runenv_image_version}

EXPOSE 8080
CMD [ "dweb", "8080" ]