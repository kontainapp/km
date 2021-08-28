# Faktory Java Demo

This shows a conversion of Java springboot container into a Kontainer.
The doc below assumes you have (1) KM git repo cloned (2) faktory and KM built (3) you are in the top of the repo.

## Build

This demo requires faktory built under `tools/faktory`.

```bash
# To build faktory tools
make -C tools/faktory

# faktory cli should exist after build
ls -l tools/faktory/bin/faktory
```

This demo requires kontain aka runenv images.

```bash
make -C payloads/java all runenv-image
```

## Demo

```sh
# Create an original docker container using spring boot. The dockerfile is
# under `demo/faktory/java/springboot/Dockerfile`.
make -C demo/faktory/java original
```

```sh
# To run the orginal container
docker run -it --rm -p 8080:8080 --name demo-original kontainapp/faktory-java-demo-original
# In another window
curl  "http://127.0.0.1:8080/greeting"

# Convert. Note: `sudo` is required because the conversion needs access to Docker daemon's image layers storage..
sudo tools/faktory/bin/faktory convert --type java \
    kontainapp/faktory-java-demo-original:latest \
    kontainapp/faktory-java-demo-kontain:latest \
    kontainapp/runenv-jdk-11.0.8:latest

# To run the kontainer. --init makes ^C/^Z killing handled by runtime
docker run -it --init --rm -p 8081:8080 \
    --name demo-kontain \
    --runtime=krun \
    kontainapp/faktory-java-demo-kontain:latest

# In another window
curl  "http://127.0.0.1:8081/greeting"

# To clean, kill the container instances and run:
make clean

```
