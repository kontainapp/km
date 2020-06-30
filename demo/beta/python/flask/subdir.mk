LOCAL_DIR := flask
LOCAL_IMAGE_NAME := docker.io/kontainapp/flask-demo-${IMAGE_VERSION}
LOCAL_DOCKER_IMAGE_TAG := ${LOCAL_IMAGE_NAME}:docker
LOCAL_KONTAIN_IMAGE_TAG := ${LOCAL_IMAGE_NAME}:kontain

${LOCAL_DIR}/docker: base ## Build flask app in docker
	${DOCKER_BUILD} -t ${LOCAL_DOCKER_IMAGE_TAG} -f ${LOCAL_DIR}/docker.dockerfile ${LOCAL_DIR} 

${LOCAL_DIR}/kontain: base ## Build flask app in kontain
	${DOCKER_BUILD} -t ${LOCAL_KONTAIN_IMAGE_TAG} -f ${LOCAL_DIR}/kontain.dockerfile ${LOCAL_DIR} 

${LOCAL_DIR}/clean:
	$(call clean_container_image,${LOCAL_DOCKER_IMAGE_TAG})
	$(call clean_container_image,${LOCAL_KONTAIN_IMAGE_TAG})