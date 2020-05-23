FROM scratch

ENV PYTHONPATH=/cpython/Lib:/cpython/build/lib.linux-x86_64-3.7
ENV PYTHONHOME=foo:bar

COPY . /
ENTRYPOINT [ "/opt/kontain/bin/km","python.km" ]