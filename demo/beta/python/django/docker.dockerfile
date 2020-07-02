FROM python:3.8-alpine

COPY app /app
RUN pip install -r /app/requirements.txt
WORKDIR /app
EXPOSE 8080

CMD [ "gunicorn", "--bind", "0.0.0.0:8080", "helloworld.wsgi" ]