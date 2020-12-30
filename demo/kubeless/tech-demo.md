# Kontain Technology Demonstrator

## Abstract

Our technology demonstration is a serverless function platform that that allows high-performance 'always zero-scale' operation by leveraging KM snapshots. The overhead for KM snapshot startup is targeted at 10 ms of less (pulled that out of my ear).

Snapshot based Kontainer:

- Static by nature. All dynamic linkages have been performed before snapshot is taken.
- Immutable. Every run is a fresh instance of the snapshot memory image
- Isolated.

## Work Items

- Is an instantiated snapshot sufficient to represent function without bring FS along?
- How does request to function call and back work? Single augmented KM process? Multiple processes?
- Concurrency?
- How are function executables stored and distributed?
- How is this scaled up?
- Metrics? Chargebacks?
- Container format? Registry? Push/Pull? OCI Images? Namespaces?

## Basic Flow

Existing kubeless produces a container that contains a the core runtime for the function (e.g. python 3.8) and the function itself. Kubeless then starts pod(s) for the container and sets up a k8s Service definition to route function requests to the pod(s). These containers are close to exactly what we want for a starting point for creating KM snapshots. They contain the user's function and it's dependencies along with the any needed language interpreter (e.g. Python).

We will modify this flow in the following ways:

- km and libc.so will be added to the containers
- We will modify the runtime's 'main' in these containers to be KM aware. By this I mean able to take the snapshot as well as handle the function when KM starts it as a snapshot.
- We will add a new step to the kubeless function container build process. The new step starts the function and creates a snapshot once the function is loaded. The snapshot, along with metadata is uploaded to a Kontain registry.
- The Kubernetes database is updated to reflect the new snapshot and metadata.
