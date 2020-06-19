
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