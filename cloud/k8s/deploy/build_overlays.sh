#!/bin/bash
set -x

# this file is only used for localized testing of kustomize outputs during edits to daemonset install
mkdir -p ./kontain-deploy/outputs/
kustomize build kontain-deploy/base > ./kontain-deploy/outputs/km.yaml
kustomize build kontain-deploy/overlays/kkm  > ./kontain-deploy/outputs/kkm.yaml
kustomize build kontain-deploy/overlays/km-crio  > ./kontain-deploy/outputs/km-crio.yaml
kustomize build kontain-deploy/overlays/k3s  > ./kontain-deploy/outputs/k3s.yaml
