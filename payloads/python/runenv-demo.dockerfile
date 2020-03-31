ARG runenv_image_version=latest

FROM kontain/runenv-python:${runenv_image_version}

COPY scripts /scripts
EXPOSE 8080
CMD ["/scripts/micro_srv.py", "8080"]