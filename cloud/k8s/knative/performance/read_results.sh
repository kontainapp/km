#
# Copyright 2021 Kontain Inc
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
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
