FROM scratch
COPY . /
ENTRYPOINT [ "/opt/kontain/bin/km", "--copyenv", "node.km" ]