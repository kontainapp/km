`python/flask` contains a simple example how users can use `kontain` python
images to build a flask application. To use this, please make sure the python
`runenv-python` image is built.

```bash

# Build python runenv image
make -C ${TOP}/payloads/python make
make -C ${TOP}/payloads/python make runenv-image

# Make sure the runenv image builds correctly
make -C ${TOP}/payloads/python make validate-runenv-image

# Build all the container targets
# * kontainapp/python:3.8
# * kontainapp/flask-demo-3.8:docker 
# * kontainapp/flask-demo-3.8:kontain
# * kontainapp/django-demo-3.8:docker 
# * kontainapp/django-demo-3.8:kontain
make

# To run kontain flask demo in the background
docker run --device=/dev/kvm -d --rm -p 8080:8080 kontainapp/flask-demo-3.7:kontain

# To clean the images:
make clean
```