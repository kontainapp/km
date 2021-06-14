ARG RUNENV_IMAGE_VERSION=latest

FROM kontainapp/runenv-python:${RUNENV_IMAGE_VERSION}

COPY scripts /scripts
EXPOSE 8080
CMD ["/usr/local/bin/python3", "/scripts/micro_srv.py", "8080"]
