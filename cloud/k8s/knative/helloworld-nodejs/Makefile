
TOP := $(shell git rev-parse --show-toplevel)
IMAGE_NAME := docker.io/kontainapp/helloworld-nodejs:latest

build:
	cp ${KM_BIN} . 
	docker build -t ${IMAGE_NAME} .
	rm -rf km

push:
	docker push ${IMAGE_NAME}

deploy:
	kubectl apply -f service.yaml

delete:
	kubectl delete -f service.yaml

clean:
	-docker rmi ${IMAGE_NAME}
	rm -rf km

include ${TOP}/make/locations.mk