# Knative performance benchmark

## Mako stub

Knative benchmark uses mako as a database and dashboard. The real mako
service is only accessible to to Google internal people, so `mako-stub` is
used. The code is located at
`https://github.com/knative/pkg/tree/master/test/mako/stub-sidecar`.

## `ko` tools
Knative projects uses `ko` tools extensively. Many of the services are
defined using this tool, so we need to install it.
`https://github.com/google/ko`

After installing, `ko` requires a docker registry and use env to define it.
Recommended to use one's own because it will create a mess in the registry.
```bash
export KO_DOCKER_REPO="docker.io/<registry>"
```

## Knative project

while some of the testing code are duplicated here for ease of use, we left
the bench mark code as is and pointing `ko` tools to them. Therefore, we need
to download the knative projects to the right place.

```bash
mkdir -p $GOPATH/src/knative.dev

cd $GOPATH/src/knative.dev

git clone git@github.com:knative/serving.git
git clone git@github.com:knative/pkg.git
```

Note: it's important that `$GOPATH` is setup correctly.

## Running benchmarks

For now, the benchmark used is the `load-test-zero` benchmark, which stresses
the knative service's scale from zero. There are a few other benchmarks, but
the framework is the same.

The following instruction assumes we have a healthy knative cluster deployed
and `kubectl` is pointing the the cluster.

We first need to create mako config in the k8s cluster: `kubectl create
configmap -n default config-mako --from-file=dev.config`. Note that
`dev.config` name can't be changed because benchmark are hardcoded to look
for it.

The we deploy benchmark:
```bash
ko create -f load-test-setup.yaml
ko create -f load-test.yaml
```

To read result, run:
```bash
bash read_results.sh
```

The script will keep retry until benchmark is finished. While it's retrying,
you may see many connection refused error. This is becasue the server to
serve the result in csv will not start until benchmark finishes.

The csv files are downloaded to `.result/out.csv`. To plot csv: `gnuplot -c
plg/latency-plot.plg .results/out.csv 0.005 480 520`