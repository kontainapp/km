FROM scratch

ENV PYTHONPATH=/cpython/Lib 
ENV PYTHONHOME=foo:bar

COPY . /
ENTRYPOINT [ "/opt/kontain/bin/km", "--copyenv", "python.km" ]