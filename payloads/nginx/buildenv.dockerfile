FROM alpine:3.8

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
