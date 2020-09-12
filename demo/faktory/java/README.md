Faktory Java Demo

# Build
This demo requires faktory built under `${TOP}/tools/faktory`.
```bash
# To build faktory tools
cd ${TOP}/tools/faktory; make

# faktory cli should exist after build
ls ${TOP}/tools/faktory/bin/faktory
```

This demo requires kontain java runenv images.
```bash
make -C ${TOP}/payloads/java runenv-image
```

# Demo
```bash
# Create an original docker container using spring boot. The dockerfile is
# under `springboot/Dockerfile`.
make original

# To run the orginal container
docker run -it --rm -p 8080:8080 --name demo-original kontainapp/faktory-java-demo-original
# In another window
curl  "http://127.0.0.1:8080/greeting"

# Convert. Note: `sudo` is required.
sudo ${TOP}/tools/faktory/bin/faktory convert --type java \
    kontainapp/faktory-java-demo-original:latest \
    kontainapp/faktory-java-demo-kontain:latest \
    kontain/runenv-jdk-11.0.8:latest

# To run the kontainer
docker run -it --rm -p 8081:8080 \
    --name demo-kontain \
    --device /dev/kvm \
    -v /opt/kontain/bin/km:/opt/kontain/bin/km:z \
    -v /opt/kontain/runtime/libc.so:/opt/kontain/runtime/libc.so:z \
    kontainapp/faktory-java-demo-kontain:latest

# In another window
curl  "http://127.0.0.1:8081/greeting"

# To clean, kill the container instances and run:
make clean

```