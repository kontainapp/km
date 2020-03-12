FROM scratch

COPY dweb /dweb
WORKDIR /dweb
ENTRYPOINT [ "/opt/kontain/bin/km", "dweb.km" ]