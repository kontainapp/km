# Knative-related code

Some demos and tryouts from investigation and  **is not**   is a working set of tools.

*if* we do future work with Knative, it will go here.

For investigation, I installed Knative into k83/Azure cluster, did a few experiments with BUILD components, deployed Python-km micro-srv.py as Knative Service, and checked cold/warm times for python.km, as well as for provided helloworld-go demo. I also read up pn opther component


## Current (as of 6/12/2019) conclusion:

* Knative is mainly focusing on long-running microservices, not one-time event-drive functions
* Kontain fast start  does not seem to be add value to Knative, at least for a while
* Kontain VM security encapsulation will be interesting when Knative is really used in production (not yet I think), but there is not much work for us beyond already planned OCI/CRI integration as they expliciutly support the both
* We need to focus on framework that takes on fast-running one-call-invokes-new-instance use cases. OpenWhisk seems to be the major one after cloud providers (AWS Lambda, AZ FUnctions. etc...)



## A few details

* Knative is mainly focusing on long-running microservices, with specific attention to CI/CD (BUILD Native component), simplifying service/route/deploy/lifecycle of a service (SERVICE group os Knative components), and tandardixing event coordinating bus (EVENTING component)
* it is great for teams developing (large) number of cooperating microservices, which need to dynamically and independently build, route, update and scale, but **IS NOT** focusing on anything real time or requiring fast start (etails below)
* Knative CAN benefit from Security implications of Kontain, but it is not an obvious problem yet, and it will be covered by OCI/CRI effort in Kontain anyways
* Knative current perf is 5-6 sec for cold start
* Knative perf **TARGETS**:
  * sub-sec cold start (see [Performance: Sub-Second Cold Start](https://github.com/knative/serving/projects/8) in https://github.com/knative/serving
  * Scale from 0 to 1000 concurrent requests in 30 sec or less https://github.com/knative/serving/blob/master/docs/scaling/DEVELOPMENT.md
   * it actually means 30 sec for 1K replicas, i.e. 33replicas/sec. However, nothing prevents burst creation of replicas, so each individal one coming up in a few secs is not an issue
* Knative perf-related issues (some of them, to illustrate)
  * https://github.com/knative/serving/issues/1297 - Excessive pod startup times (> 10s)
  * https://github.com/knative/serving/issues/1345 (envoy adds 5-6 sec to startup) - resolved by changing Envoy timeout and updatint istio, now it 1-2 sec due to networking
  * https://github.com/knative/serving/issues/2485 - Istio adds over 2seconds to startup time


## Relevant links:

* Docs https://knative.dev/docs
* Knative Autoscaling architecture https://github.com/knative/serving/blob/master/docs/scaling/DEVELOPMENT.md
* Scaling config (to zero and back) https://medium.com/@kamesh_sampath/knative-as-a-pod-time-machine-3c1ca0cfb48a

## Demo (May 18th, 2020)

Goal: deploy the current kontain stack on AKS showing a functional demo.

### Set up

First, launch a new AKS cluster. Use `cloud/azure/aks_ci_create.sh` to create
a new aks cluster. The script requires a service principle appid and token.
After the cluster is launched successfully, run 
`az aks get-credentials --resource-group kontainKubeRG --name <name of the cluster>`
to get credential.

Second, deploy `istio` onto the cluster. Use the instruction
[here](https://istio.io/docs/setup/getting-started/).

Third, install `knative`. Use the instruction
[here](https://knative.dev/docs/install/any-kubernetes-cluster/). Skip the
part of `istio` in this instruction because it's deprecated as of now.

Fourth, disable container tag resolution. To access images from azure
container registry, AKS will embed a service principle into the cluster, for
accessing images from ACR. The secret is loaded on the machine where kubelet
runs, so kubelet can correctly pull the image with the right authentication.
Normally, this works because kubelet is the only service need to pull
container images. In knative, the control plane services also want to pull
the metadata of the image to support the "revision" feature. However, there
requires complicated work arounds. For now, use the instruction
[here](https://knative.dev/docs/serving/tag-resolution/) to disable this
feature. Another
[ref](https://github.com/knative/serving/issues/6114#issuecomment-619262974)
to the issue. Use `kubectl get pods --namespace knative-serving` to make sure
all the knative components are up.

Finally, to deploy an knative service, use the following yaml.

```yaml
apiVersion: serving.knative.dev/v1 # Current version of Knative
kind: Service
metadata:
  name: helloworld-go # The name of the app
  namespace: default # The namespace the app will use
spec:
  template:
    spec:
      containers:
        - image: kontainkubecr.azurecr.io/test-python-fedora:ci-nightly-2250 # The URL to the image of the app
          env:
            - name: TARGET # The environment variable printed out by the sample app
              value: "Go Sample v1"
          command:
            ["./km", "--copyenv", "python.km", "scripts/micro_srv.py", "8080"]
          resources:
            requests:
              devices.kubevirt.io/kvm: "1"
              cpu: "250m"
            limits:
              devices.kubevirt.io/kvm: "1"
              cpu: "1"
```

After the setup, to check the deployment is ok. 
```bash
# Note the external IP and port of istio ingress gateway. For example:
# 192.168.39.228:32198
kubectl --namespace istio-system get service istio-ingressgateway

# Note the URL of the service deployed. For example:
# helloworld-go.default.example.com
kubectl get ksvc

# Use curl to test in this way:
curl -H "Host: helloworld-go.default.example.com" http://192.168.39.228:32198
```

Using curl is a workaround to setting up a real DNS for the services. The
instruction is
[here](https://knative.dev/docs/install/any-kubernetes-cluster/) under
`configure DNS` section.

### Notes:
1. Knative uses service mesh for configuring network, and this means sidecar
pattern.
2. Volume mount is disallowed except for configmaps and secrets.
Specifically, hostPath is disallowed for knative. This is also the reason I’m
using `testenv-python-fedora` instead of `runenv-python` images. For now, we
need to package km inside the docker container.
3. Device plugin works out of box, so that’s good.
