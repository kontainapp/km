#!/bin/bash
set -x
kustomize build kontain-deploy/base > ./kontain-deploy/outputs/base.yaml
kustomize build kontain-deploy/overlays/kkm  > ./kontain-deploy/outputs/kkm.yaml
kustomize build kontain-deploy/overlays/km-crio  > ./kontain-deploy/outputs/km-crio.yaml