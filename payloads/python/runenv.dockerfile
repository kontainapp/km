FROM scratch

# Python version must be passed obn build
ARG VERS

ENV PYTHONPATH=/cpython/Lib:/cpython/build/lib.linux-x86_64-${VERS}
ENV PYTHONHOME=foo:bar

COPY . /
ENTRYPOINT [ "/usr/local/bin/python3" ]