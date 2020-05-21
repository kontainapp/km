# Kontain Knative Nodejs Helloworld Demo

## Requirement

1. AKS cluster with knative deployed.
2. Deploy kontaind for kvm device plugin.
3. Access to dockerhub if you want to push the demo image.

## Steps

```bash
# Build docker image
make image-build

# Push to kontainapp docker hub
make image-push

# Deploy to knative
make knative-deploy

# Delete from knative
make knative-delete
```