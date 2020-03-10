FROM scratch
COPY . /
ENTRYPOINT [ "/km", "--copyenv" ]