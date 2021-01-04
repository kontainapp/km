# Kontain Technology Demonstrator

## Abstract

Our technology demonstration is a serverless function platform that that allows high-performance 'always zero-scale' operation by leveraging KM snapshots. The overhead for KM snapshot startup is targeted at 10 ms of less (pulled that out of my ear).

Snapshot based Kontainer:

- Static by nature. All dynamic linkages have been performed before snapshot is taken.
- Immutable. Every run is a fresh instance of the snapshot memory image
- Isolated.

## Work Items

Investigate and decide on the following:

- Is an instantiated snapshot sufficient to represent function without bring FS along?
- How does request to function call and back work? Single augmented KM process? Multiple processes?
- Concurrency?
- How are function executables stored and distributed?
- How is this scaled up?
- Metrics? Chargebacks?
- Kubeless CRD modification (add Kontain info) and related Kubeless service modification (to correctly create Kontain-based functions)
- For container images, we prefer to keep using existing OCI images infrastructure including container registries. The main question is how to tell Kontain images from others ? Most likely labels.

## Basic Flow

Existing kubeless produces a container that contains a the core runtime for the function (e.g. python 3.8) and the function itself. Both runtime and the function code are provided to Kubeless from outside (e.g. customer, or one of pre-defined runtime images).

Kubeless then starts pod(s) with the container and sets up a k8s Service definition to route function requests to the pod(s). These containers are close to exactly what we want for a starting point for creating KM snapshots. They contain the user's function and it's dependencies along with the any needed language interpreter (e.g. Python).

As the first step, we will will place 'km' inside of the runtime images, and modify this flow in the following ways:

- We will add km and libc.so to the container images
- We will modify the runtime's 'main' in these containers to be KM aware. By this I mean able to take the snapshot as well as handle the function when KM starts it as a snapshot.
- We will add a new step to the kubeless function container build process. The new step starts the function and creates a snapshot once the function is loaded. The snapshot is then used to create a new container image for the function, and the new imagw is uploaded to a Kontain registry.
- The Kubernetes database is updated to reflect the new snapshot and metadata. Most likely we will modify Kubeless CRDs to know about Kontain
