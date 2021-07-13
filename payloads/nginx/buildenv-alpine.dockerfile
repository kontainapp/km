# Copyright © 2019 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.


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
