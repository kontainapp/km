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

pod_name=load-test-zero
mako_namespace=default
output_file=.results/out.csv

bash "$GOPATH/src/knative.dev/pkg/test/mako/stub-sidecar/read_results.sh" \
    "$pod_name" \
    "$mako_namespace" \
    ${mako_port:-10001} \
    ${timeout:-120} \
    ${retries:-100} \
    ${retries_interval:-10} \
    "$output_file"
