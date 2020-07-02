FROM docker.io/kontainapp/python:3.8

COPY app /app
RUN pip3 install -r /app/requirements.txt
WORKDIR /app
EXPOSE 8080

# TODO: remove this hack in the future
RUN mv /usr/bin/python3.8 /usr/bin/python3.8.old && \
    ln -s /opt/kontain/bin/python3.8 /usr/bin/python3.8

CMD [ "gunicorn", "--bind", "0.0.0.0:8080", "helloworld.wsgi" ]