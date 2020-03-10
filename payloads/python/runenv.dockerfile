FROM scratch

ENV PYTHONPATH=/cpython/Lib 
ENV PYTHONHOME=foo:bar

COPY . /
ENTRYPOINT [ "/km", "--copyenv", "python.km" ]