#!/bin/bash
#
#  Copyright © 2021 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.
#
#
# Link redis.kmd
set -e ; [ "$TRACE" ] && set -x

TOP=$(git rev-parse --show-toplevel)
KONTAIN_GCC=${TOP}/tools/bin/kontain-gcc
CURRENT=${TOP}/payloads/redis
REDIS_TOP=${CURRENT}/redis
REDIS_SRC=${REDIS_TOP}/src

${KONTAIN_GCC} -g -ggdb -rdynamic \
    -o ${CURRENT}/redis-server.kmd \
    ${REDIS_SRC}/adlist.o \
    ${REDIS_SRC}/quicklist.o \
    ${REDIS_SRC}/ae.o \
    ${REDIS_SRC}/anet.o \
    ${REDIS_SRC}/dict.o \
    ${REDIS_SRC}/server.o \
    ${REDIS_SRC}/sds.o \
    ${REDIS_SRC}/zmalloc.o \
    ${REDIS_SRC}/lzf_c.o \
    ${REDIS_SRC}/lzf_d.o \
    ${REDIS_SRC}/pqsort.o \
    ${REDIS_SRC}/zipmap.o \
    ${REDIS_SRC}/sha1.o \
    ${REDIS_SRC}/ziplist.o \
    ${REDIS_SRC}/release.o \
    ${REDIS_SRC}/networking.o \
    ${REDIS_SRC}/util.o \
    ${REDIS_SRC}/object.o \
    ${REDIS_SRC}/db.o \
    ${REDIS_SRC}/replication.o \
    ${REDIS_SRC}/rdb.o \
    ${REDIS_SRC}/t_string.o \
    ${REDIS_SRC}/t_list.o \
    ${REDIS_SRC}/t_set.o \
    ${REDIS_SRC}/t_zset.o \
    ${REDIS_SRC}/t_hash.o \
    ${REDIS_SRC}/config.o \
    ${REDIS_SRC}/aof.o \
    ${REDIS_SRC}/pubsub.o \
    ${REDIS_SRC}/multi.o \
    ${REDIS_SRC}/debug.o \
    ${REDIS_SRC}/sort.o \
    ${REDIS_SRC}/intset.o \
    ${REDIS_SRC}/syncio.o \
    ${REDIS_SRC}/cluster.o \
    ${REDIS_SRC}/crc16.o \
    ${REDIS_SRC}/endianconv.o \
    ${REDIS_SRC}/slowlog.o \
    ${REDIS_SRC}/scripting.o \
    ${REDIS_SRC}/bio.o \
    ${REDIS_SRC}/rio.o \
    ${REDIS_SRC}/rand.o \
    ${REDIS_SRC}/memtest.o \
    ${REDIS_SRC}/crc64.o \
    ${REDIS_SRC}/bitops.o \
    ${REDIS_SRC}/sentinel.o \
    ${REDIS_SRC}/notify.o \
    ${REDIS_SRC}/setproctitle.o \
    ${REDIS_SRC}/blocked.o \
    ${REDIS_SRC}/hyperloglog.o \
    ${REDIS_SRC}/latency.o \
    ${REDIS_SRC}/sparkline.o \
    ${REDIS_SRC}/redis-check-rdb.o \
    ${REDIS_SRC}/redis-check-aof.o \
    ${REDIS_SRC}/geo.o \
    ${REDIS_SRC}/lazyfree.o \
    ${REDIS_SRC}/module.o \
    ${REDIS_SRC}/evict.o \
    ${REDIS_SRC}/expire.o \
    ${REDIS_SRC}/geohash.o \
    ${REDIS_SRC}/geohash_helper.o \
    ${REDIS_SRC}/childinfo.o \
    ${REDIS_SRC}/defrag.o \
    ${REDIS_SRC}/siphash.o \
    ${REDIS_SRC}/rax.o \
    ${REDIS_SRC}/t_stream.o \
    ${REDIS_SRC}/listpack.o \
    ${REDIS_SRC}/localtime.o \
    ${REDIS_SRC}/lolwut.o \
    ${REDIS_SRC}/lolwut5.o \
    ${REDIS_SRC}/lolwut6.o \
    ${REDIS_SRC}/acl.o \
    ${REDIS_SRC}/gopher.o \
    ${REDIS_SRC}/tracking.o \
    ${REDIS_SRC}/connection.o \
    ${REDIS_SRC}/tls.o \
    ${REDIS_SRC}/sha256.o \
    ${REDIS_SRC}/../deps/hiredis/libhiredis.a \
    ${REDIS_SRC}/../deps/lua/src/liblua.a \
    ${REDIS_SRC}/../deps/jemalloc/lib/libjemalloc.a \
    -lm -ldl -pthread -lrt
