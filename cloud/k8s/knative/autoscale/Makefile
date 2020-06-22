
TOP := $(shell git rev-parse --show-toplevel)
IMAGE_NAME := docker.io/kontainapp/autoscale:latest

payload-build:
	CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go build -ldflags "-T 0x201000 -w -extldflags '-no-pie -static -Wl,--gc-sections'" -o autoscale.km autoscale.go

image-build:
	cp ${KM_BIN} . 
	docker build -t ${IMAGE_NAME} .
	rm -rf km

image-push:
	docker push ${IMAGE_NAME}

clean:
	-docker rmi ${IMAGE_NAME}
	rm -rf km
	rm -rf autoscale.km

include ${TOP}/make/locations.mk