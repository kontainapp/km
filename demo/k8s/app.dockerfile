FROM kontainapp/runenv-python

COPY app.sh /app.sh

CMD [ "/bin/sh", "/app.sh" ]
