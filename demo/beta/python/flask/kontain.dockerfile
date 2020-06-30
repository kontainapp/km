FROM docker.io/kontainapp/python:3.8

RUN pip3 install flask gunicorn
COPY app /app
WORKDIR /app
EXPOSE 8080

# TODO: properly fix this in the python base image instead of here.
# 
# We want to replace /usr/bin/python3.8 to km python so programs such as
# gunicorn can directly refer to /usr/bin/python. Ideally, this should be done
# in the `kontainapp/python` image. However, pip also uses /usr/bin/python.
# When building an application container such as this one, we don't have access
# to /dev/kvm. As result, we can't run km python during container build due to
# lack of /dev/kvm.
RUN mv /usr/bin/python3.8 /usr/bin/python3.8.old && \
    ln -s /opt/kontain/bin/km /usr/bin/python3.8

CMD [ "gunicorn", "--bind", "0.0.0.0:8080", "app:app" ]