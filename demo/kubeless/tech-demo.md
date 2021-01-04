# Kontain Technology Demonstrator

## Abstract

Our technology demonstration is a serverless function platform that that allows high-performance 'always zero-scale' operation by leveraging KM snapshots. The overhead for KM snapshot startup is targeted at 10 ms of less (pulled that out of my ear).

Snapshot based Kontainer:

- Static by nature. All dynamic linkages have been performed before snapshot is taken.
- Immutable. Every run is a fresh instance of the snapshot memory image
- Isolated.

## Design Outline

Kubeless already builds a container for each function it knows about. This container includes the user's function, it's dependencies, and a Kubeless defined language runtime. This is the perfect starting point for creating a KM snapshot of the running user function. We only need to do two things to make this work:

- Add KM to the function container
- Ensure the language runtime is something KM can support. For example from our `payloads` directory or something built for `alpine/musl`.

Once a function container is created, Kubeless deploys it into one or more pods. We want to replace that with 1) start the function container, 2) wait for the function to come up, 3) take the KM snapshot, 4) upload the snapshot to a to-be-defined registry, and 5) stop the function container. Step 2 is easy since we can leverage the existing kubernetes health check infrastructure to know when the function is up.

Once the snapshot is created and uploaded, we don't want to continue running the function container. We want to inform Kontain workers (new component) that the new function and snapshot exists. The workers will then download the snapshot from the registry and start handing requests for the function by running the snapshot.

This new Kontain worker component needs to see all function call requests and it need a way to map individual requests to specific functions. The mechanism to do this still needs to be designed (port number and http patch prefixes seem to be the obvious choices). Once the target function is identified the snapshot is started. Ideally the snapshot would only run for as long as it takes to get the function result. In order to make this work well, the function's 'main' handing will be our code and it will be snapshot aware.

For scale up, multiple instances of the Kontain worker can be instantiated in a cluster.

## Work Items

Investigate and decide on the following:

- Is an instantiated snapshot sufficient to represent function without bring FS along? (Serge)
- How does request to function call and back work? Single augmented KM process? Multiple processes?
- Concurrency?
- How are function executables stored and distributed?
- How is this scaled up?
- Metrics? Chargebacks?
- k8s Service definitions and how function calls are routed.
- Kubeless CRD modification (add Kontain info) and related Kubeless service modification (to correctly create Kontain-based functions)
- For container images, we prefer to keep using existing OCI images infrastructure including container registries. The main question is how to tell Kontain images from others ? Most likely labels.

## Possible Work Plan

In order to make this tractable, we should start with a single runtime, for example the python we build in `payload/python`.

Step 1: Modify the existing structure to allow a snapshot to be created and uploaded to a registry when the function container comes up. The trigger could be something as stupid as an http request to the function's running container (this would require us to modify the runtime main).

Step 2: Write our new worker. There are two pieces to it:

- Something that watches the Kubernetes data store for changes to functions. When it sees one it downloads the associated snapshot locally
- Something that takes requests, maps each request to an individual snapshot, and runs the snapshot.

Step 3: Write Service rules to route function requests to the Kontain worker(s). Initially we could add a prefix (or suffix) to the function's exposed name so our stuff and Kubeless 'classic' could run side-by side.

Step 4: Expand to more runtimes.

Step 5: Turn off Kubeless 'classic' in favor of our new stuff.
