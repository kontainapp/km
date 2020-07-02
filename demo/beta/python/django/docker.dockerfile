FROM python:3.8-alpine

COPY app /app
RUN pip install /app/requirement.txt
WORKDIR /app
EXPOSE 8080
# CMD [ "gunicorn", "--bind", "0.0.0.0:8080", "app:app" ]
CMD [ "python", "manage.py", "runserver" ]