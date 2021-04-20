FROM alpine

# Python version must be passed on build
ARG VERS

COPY . /

#
# In python.km, we look for lib64 (to comply with Fedora layout), so we need to
# make sure the python files which we MAY get from other containers, are found
RUN ln -s /usr/local/lib /usr/local/lib64

ENTRYPOINT [ "/usr/local/bin/python3" ]
