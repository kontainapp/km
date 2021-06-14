ARG runenv_image_version=latest

FROM kontainapp/runenv-python:${runenv_image_version}

COPY scripts /scripts
EXPOSE 8080
CMD ["/usr/local/bin/python3", "/scripts/micro_srv.py", "8080"]
