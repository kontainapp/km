FROM alpine:3.8

RUN apk add --no-cache --virtual .build-deps \
	coreutils \
	gcc \
	linux-headers \
	make \
	musl-dev \
	openssl-dev
