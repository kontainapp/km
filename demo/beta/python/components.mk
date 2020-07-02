${COMPONENT}_PATH := $(realpath ${COMPONENT})
${COMPONENT}_IMAGE_NAME := docker.io/kontainapp/${COMPONENT}-demo-${IMAGE_VERSION}
${COMPONENT}_DOCKER_IMAGE_TAG := ${${COMPONENT}_IMAGE_NAME}:docker
${COMPONENT}_KONTAIN_IMAGE_TAG := ${${COMPONENT}_IMAGE_NAME}:kontain

${COMPONENT}/docker: COMPONENT := ${COMPONENT}
${COMPONENT}/docker: base
	$(call clean_container_image,${${COMPONENT}_DOCKER_IMAGE_TAG})
	${DOCKER_BUILD} -t ${${COMPONENT}_DOCKER_IMAGE_TAG} -f ${${COMPONENT}_PATH}/docker.dockerfile ${${COMPONENT}_PATH} 

${COMPONENT}/kontain: COMPONENT := ${COMPONENT}
${COMPONENT}/kontain: base
	$(call clean_container_image,${${COMPONENT}_KONTAIN_IMAGE_TAG})
	${DOCKER_BUILD} -t ${${COMPONENT}_KONTAIN_IMAGE_TAG} -f ${${COMPONENT}_PATH}/kontain.dockerfile ${${COMPONENT}_PATH} 

${COMPONENT}/clean: COMPONENT := ${COMPONENT}
${COMPONENT}/clean:
	$(call clean_container_image,${${COMPONENT}_DOCKER_IMAGE_TAG})
	$(call clean_container_image,${${COMPONENT}_KONTAIN_IMAGE_TAG})