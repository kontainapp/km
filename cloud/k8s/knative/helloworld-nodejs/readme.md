# Kontain Knative Nodejs Helloworld Demo

## Requirement

1. AKS cluster with knative deployed.
2. Deploy kontaind for kvm device plugin.
3. Access to dockerhub if you want to push the demo image.

## Steps

```bash
# Build docker image
make build

# Push to kontainapp docker hub
make push

# Deploy to knative
make deploy

# Delete from knative
make delete
```