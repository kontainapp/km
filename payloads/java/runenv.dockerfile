FROM scratch
COPY . /
ENTRYPOINT [ "/km", "--copyenv", "node.km" ]