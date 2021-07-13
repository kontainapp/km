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
# Link nginx.kmd

set -e
[ "$TRACE" ] && set -x

NGINX_OBJ_LOC=${1?please pass nginx object files dir arg1}
KONTAIN_TOOLS_LOC=${2?please pass kontain-gcc dir as arg2}
NGINX_KM_LOC=${3?please pass where to place nginx.km as arg3}

NGINX_OBJS=$(
   cat <<END_OF_LIST
    ${NGINX_OBJ_LOC}/src/core/nginx.o
    ${NGINX_OBJ_LOC}/src/core/ngx_log.o
    ${NGINX_OBJ_LOC}/src/core/ngx_palloc.o
    ${NGINX_OBJ_LOC}/src/core/ngx_array.o
    ${NGINX_OBJ_LOC}/src/core/ngx_list.o
    ${NGINX_OBJ_LOC}/src/core/ngx_hash.o
    ${NGINX_OBJ_LOC}/src/core/ngx_buf.o
    ${NGINX_OBJ_LOC}/src/core/ngx_queue.o
    ${NGINX_OBJ_LOC}/src/core/ngx_output_chain.o
    ${NGINX_OBJ_LOC}/src/core/ngx_string.o
    ${NGINX_OBJ_LOC}/src/core/ngx_parse.o
    ${NGINX_OBJ_LOC}/src/core/ngx_parse_time.o
    ${NGINX_OBJ_LOC}/src/core/ngx_inet.o
    ${NGINX_OBJ_LOC}/src/core/ngx_file.o
    ${NGINX_OBJ_LOC}/src/core/ngx_crc32.o
    ${NGINX_OBJ_LOC}/src/core/ngx_murmurhash.o
    ${NGINX_OBJ_LOC}/src/core/ngx_md5.o
    ${NGINX_OBJ_LOC}/src/core/ngx_sha1.o
    ${NGINX_OBJ_LOC}/src/core/ngx_rbtree.o
    ${NGINX_OBJ_LOC}/src/core/ngx_radix_tree.o
    ${NGINX_OBJ_LOC}/src/core/ngx_slab.o
    ${NGINX_OBJ_LOC}/src/core/ngx_times.o
    ${NGINX_OBJ_LOC}/src/core/ngx_shmtx.o
    ${NGINX_OBJ_LOC}/src/core/ngx_connection.o
    ${NGINX_OBJ_LOC}/src/core/ngx_cycle.o
    ${NGINX_OBJ_LOC}/src/core/ngx_spinlock.o
    ${NGINX_OBJ_LOC}/src/core/ngx_rwlock.o
    ${NGINX_OBJ_LOC}/src/core/ngx_cpuinfo.o
    ${NGINX_OBJ_LOC}/src/core/ngx_conf_file.o
    ${NGINX_OBJ_LOC}/src/core/ngx_module.o
    ${NGINX_OBJ_LOC}/src/core/ngx_resolver.o
    ${NGINX_OBJ_LOC}/src/core/ngx_open_file_cache.o
    ${NGINX_OBJ_LOC}/src/core/ngx_crypt.o
    ${NGINX_OBJ_LOC}/src/core/ngx_proxy_protocol.o
    ${NGINX_OBJ_LOC}/src/core/ngx_syslog.o
    ${NGINX_OBJ_LOC}/src/event/ngx_event.o
    ${NGINX_OBJ_LOC}/src/event/ngx_event_timer.o
    ${NGINX_OBJ_LOC}/src/event/ngx_event_posted.o
    ${NGINX_OBJ_LOC}/src/event/ngx_event_accept.o
    ${NGINX_OBJ_LOC}/src/event/ngx_event_udp.o
    ${NGINX_OBJ_LOC}/src/event/ngx_event_connect.o
    ${NGINX_OBJ_LOC}/src/event/ngx_event_pipe.o
    ${NGINX_OBJ_LOC}/src/os/unix/ngx_time.o
    ${NGINX_OBJ_LOC}/src/os/unix/ngx_errno.o
    ${NGINX_OBJ_LOC}/src/os/unix/ngx_alloc.o
    ${NGINX_OBJ_LOC}/src/os/unix/ngx_files.o
    ${NGINX_OBJ_LOC}/src/os/unix/ngx_socket.o
    ${NGINX_OBJ_LOC}/src/os/unix/ngx_recv.o
    ${NGINX_OBJ_LOC}/src/os/unix/ngx_readv_chain.o
    ${NGINX_OBJ_LOC}/src/os/unix/ngx_udp_recv.o
    ${NGINX_OBJ_LOC}/src/os/unix/ngx_send.o
    ${NGINX_OBJ_LOC}/src/os/unix/ngx_writev_chain.o
    ${NGINX_OBJ_LOC}/src/os/unix/ngx_udp_send.o
    ${NGINX_OBJ_LOC}/src/os/unix/ngx_udp_sendmsg_chain.o
    ${NGINX_OBJ_LOC}/src/os/unix/ngx_channel.o
    ${NGINX_OBJ_LOC}/src/os/unix/ngx_shmem.o
    ${NGINX_OBJ_LOC}/src/os/unix/ngx_process.o
    ${NGINX_OBJ_LOC}/src/os/unix/ngx_daemon.o
    ${NGINX_OBJ_LOC}/src/os/unix/ngx_setaffinity.o
    ${NGINX_OBJ_LOC}/src/os/unix/ngx_setproctitle.o
    ${NGINX_OBJ_LOC}/src/os/unix/ngx_posix_init.o
    ${NGINX_OBJ_LOC}/src/os/unix/ngx_user.o
    ${NGINX_OBJ_LOC}/src/os/unix/ngx_dlopen.o
    ${NGINX_OBJ_LOC}/src/os/unix/ngx_process_cycle.o
    ${NGINX_OBJ_LOC}/src/os/unix/ngx_linux_init.o
    ${NGINX_OBJ_LOC}/src/event/modules/ngx_epoll_module.o
    ${NGINX_OBJ_LOC}/src/os/unix/ngx_linux_sendfile_chain.o
    ${NGINX_OBJ_LOC}/src/core/ngx_regex.o
    ${NGINX_OBJ_LOC}/src/http/ngx_http.o
    ${NGINX_OBJ_LOC}/src/http/ngx_http_core_module.o
    ${NGINX_OBJ_LOC}/src/http/ngx_http_special_response.o
    ${NGINX_OBJ_LOC}/src/http/ngx_http_request.o
    ${NGINX_OBJ_LOC}/src/http/ngx_http_parse.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_log_module.o
    ${NGINX_OBJ_LOC}/src/http/ngx_http_request_body.o
    ${NGINX_OBJ_LOC}/src/http/ngx_http_variables.o
    ${NGINX_OBJ_LOC}/src/http/ngx_http_script.o
    ${NGINX_OBJ_LOC}/src/http/ngx_http_upstream.o
    ${NGINX_OBJ_LOC}/src/http/ngx_http_upstream_round_robin.o
    ${NGINX_OBJ_LOC}/src/http/ngx_http_file_cache.o
    ${NGINX_OBJ_LOC}/src/http/ngx_http_write_filter_module.o
    ${NGINX_OBJ_LOC}/src/http/ngx_http_header_filter_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_chunked_filter_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_range_filter_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_gzip_filter_module.o
    ${NGINX_OBJ_LOC}/src/http/ngx_http_postpone_filter_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_ssi_filter_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_charset_filter_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_userid_filter_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_headers_filter_module.o
    ${NGINX_OBJ_LOC}/src/http/ngx_http_copy_filter_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_not_modified_filter_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_static_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_autoindex_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_index_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_mirror_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_try_files_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_auth_basic_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_access_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_limit_conn_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_limit_req_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_geo_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_map_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_split_clients_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_referer_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_rewrite_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_proxy_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_fastcgi_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_uwsgi_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_scgi_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_memcached_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_empty_gif_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_browser_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_upstream_hash_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_upstream_ip_hash_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_upstream_least_conn_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_upstream_random_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_upstream_keepalive_module.o
    ${NGINX_OBJ_LOC}/src/http/modules/ngx_http_upstream_zone_module.o
    ${NGINX_OBJ_LOC}/ngx_modules.o
END_OF_LIST
)

# echo kontain-gcc -pthread -o ${NGINX}/nginx.km ${NGINX_OBJS} -lpcre -lz
${KONTAIN_TOOLS_LOC}/kontain-gcc -pthread -o ${NGINX_KM_LOC}/nginx.km ${NGINX_OBJS} -lpcre -lz
ln -sf /opt/kontain/bin/km ${NGINX_KM_LOC}/nginx
