# Kontain Integration Into Kubernetes

* See [Quick Start Guide](quickstart.md) for a demonstration of Kontain integrated with Kubernetes.
* See [Architecture Guide](architecture.md) for the Kubertetes Integration Architecture
* See [Installation Guide](installation.md) for Kubernetes Host Installation Instructions

Kontain levereges the Runtime Class abstraction to allow containers controlled by the Kontain Monitor to exist
in Kubernetes clusters. Users define containers controlled by the Kontain Monitor by adding `runtimeClassName: kontain`
to YAML container specifications.
