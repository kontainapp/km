FROM python:3.8-alpine

RUN pip install flask gunicorn
COPY app /app
WORKDIR /app
EXPOSE 8080
CMD [ "gunicorn", "--bind", "0.0.0.0:8080", "app:app" ] 