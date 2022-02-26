#!/bin/bash
set -x
mkdir -p ./kontain-deploy/outputs/
kustomize build kontain-deploy/base > ./kontain-deploy/outputs/base.yaml
kustomize build kontain-deploy/overlays/kkm  > ./kontain-deploy/outputs/kkm.yaml
kustomize build kontain-deploy/overlays/km-crio  > ./kontain-deploy/outputs/km-crio.yaml
kustomize build kontain-deploy/overlays/k3s  > ./kontain-deploy/outputs/k3s.yaml
