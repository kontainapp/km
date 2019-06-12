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