#
#   create kontain container from docker container. for python
#

FROM flex:alpine as modules_list

# TBD: copy it from python-km KM container to avoid the need to git clone :-)
ADD get_module_name.py /tmp
RUN /tmp/get_module_name.py  -o /tmp/list # generates list of modules used

# for mo

FROM python-km


#
# below is a text - sketch for workflow/design
#
flex:alpine - source (regualar) container
- scan the code and generate list of used modules, including "does it have .so"
FROM python-km
- get a list of installed modules
- foreach module
python-only: install
.so: check if known, if it is install kontain-<module>

# 1st step - copy all over.... still need a list of .so

success:
- build new python.km (in buildenv) [step 1: precreate python.km with a few .so]


result:
flex-kontain:alpine (add kontain to image name. e.g. myapp:v12 becomes myapp-kontain:v12)
FROM km # temporary - will be replaced with preinstall of KM - meaning runtime ->runk->KM start from docker/kubernetes, so it does not need to be in container

# example on how to invoke
# km --putenv PYTHONHOME=/opt/kontain ~/.local/bin/python.km -c 'import flask'
ENTRYPOINT ["/opt/kontain/bin/km", "--putenv=PYTHONHOME=/opt/kontain"]
CMD ["flask_hello.py"]