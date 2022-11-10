# How to use light snapshot start

## Introduction

The point of this feature is to speed up starting functions from snapshot with scale to zero feature.
Scale to zero implementations suffer from long container/pod setup time.
Which makes zero to one transition way to slow.

The solution is to keep the infrastructure, like k8s or knative, think that they do not scale to zero.
In other words there is a pod that always running for each function/service.
However km in each pod doesn't run the payload right away.
Instead it waits for the triggering connection and only starts the payload snapshot when the request arrives.

## Making the snapshot

Preparing the snapshot for this feature is the same process as for regular snapshot.
In addition to `kmsnap` file km now creates another file called `kmsnap.conf`.
This file records the info about the listening socket.
It looks like this:

```txt
i6 8080 ::
```

## Enabling lightweight start

The content of the conf file needs to be set as environment variable SNAP_LISTEN_PORT:

```bash
docker run ... --env=SNAP_LISTEN_PORT="i6 8080 ::" ...
```

or equivalent yaml stanza for k8s.

## Shrinking payload back

Once the payload is triggered it serves the triggering request, and keeps running after that.
If any new request comes it will serve it too.

In order to take full advantage an idle service should scale to zero.
We achieve that by a timeout on the incoming requests.
The timeout is specified by another environment variable setting the timeout in milliseconds:

```bash
docker run ... --env=SNAP_LISTEN_TIMEOUT=1000 --env=SNAP_LISTEN_PORT="i6 8080 ::" ...
```

This way we allow a payload to idle for 1 sec (1000ms) and then shrink back to minimal waiting state.
