# This file is only used to create an image for manual build and experiments

FROM alpine

RUN apk add --no-cache --virtual .build-deps \
	coreutils \
	gcc \
	linux-headers \
	make \
	musl-dev \
	openssl-dev
