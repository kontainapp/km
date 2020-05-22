FROM kontain/runenv-node
EXPOSE 8080
COPY km /opt/kontain/bin/km
COPY helloworld.js /opt/kontain/bin/helloworld.js
ENTRYPOINT [ "/opt/kontain/bin/km", "--copyenv", "/node.km", "/opt/kontain/bin/helloworld.js" ]