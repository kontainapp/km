# This file is only used to create an image for manual build and experiments

FROM alpine

RUN apk add --no-cache --virtual .build-deps \
	libc-dev \
	make \
	openssl-dev \
	pcre-dev \
	zlib-dev \
	linux-headers \
	libxslt-dev \
	gd-dev \
	geoip-dev \
	perl-dev \
	libedit-dev \
	alpine-sdk \
	findutils
