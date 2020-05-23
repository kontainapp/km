# For more information, please refer to https://aka.ms/vscode-docker-python
FROM python:3.8-alpine

EXPOSE 8000

# Keeps Python from generating .pyc files in the container
ENV PYTHONDONTWRITEBYTECODE 1

# Turns off buffering for easier container logging
ENV PYTHONUNBUFFERED 1

# Install pip requirements
ADD requirements.txt .
RUN python -m pip install -r requirements.txt

WORKDIR /app
ADD . /app

# Switching to a non-root user, please refer to https://aka.ms/vscode-docker-python-user-rights
RUN adduser --disabled-password appuser && chown -R appuser /app
RUN mkdir -p /usr/local/kontain/payloads
ENV PATH=/usr/local/kontain/payloads:${PATH}
RUN mv /usr/local/bin/python /usr/local/bin/python.km
RUN ln -s /opt/kontain/bin/km /usr/local/bin/python
ENV KM_VERBOSE=generic
# TODO  ln -s python3.8 python.km
USER appuser
# During debugging, this entry point will be overridden. For more information, please refer to https://aka.ms/vscode-docker-python-debug
CMD ["gunicorn", "--bind", "0.0.0.0:8000", "helloworld.wsgi"]
