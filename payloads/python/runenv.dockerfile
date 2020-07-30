FROM scratch

# Python version must be passed obn build
ARG VERS

COPY . /
ENTRYPOINT [ "/usr/local/bin/python3" ]
