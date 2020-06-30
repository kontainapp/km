# TODO: adds python version tag when runenv-python implements it.
FROM kontain/runenv-python as kontain_runenv_python

FROM alpine:3.11

ARG VERS

RUN apk add python3 && \
    pip3 install --upgrade pip

COPY km pyvenv.cfg /opt/kontain/bin/
COPY --from=kontain_runenv_python /python.km /opt/kontain/bin/python.km
COPY --from=kontain_runenv_python /cpython /cpython

# Configure pip default targets so we don't need to run pip -t <target>
ENV PIP_TARGET=/cpython/Lib

# Configure path to inclde binaries installed through pip
ENV PATH=/cpython/Lib/bin:${PATH}