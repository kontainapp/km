FROM alpine

# Python version must be passed on build
ARG VERS

COPY . /
ENTRYPOINT [ "/usr/local/bin/python3" ]
