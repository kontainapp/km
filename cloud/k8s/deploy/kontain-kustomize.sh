# Copyright 2022 Kontain
# Derived from:
# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#!/bin/bash
[ "$TRACE" ] && set -x

set -x
tag=""
location=""

print_help() {
    echo "usage: $0  [options]"
    echo ""
    echo "Deploys all kustomizations necessary for Kontain in your cluster"
    echo ""
    echo "Options:"
    echo "  --tag - Kontain release tag to use"
    echo "  --location - location of kontain-deploy directory"
    echo "  --help(-h) - prints this message"
    echo "***Note: only --release-tag or --location maybe specified but not both. "
    echo "         If no parametes are specifies, current release will be used"

    exit 1
}

arg_count=$#
for arg in "$@"
do
   case "$arg" in
        --release-tag=*)
            tag="${1#*=}"
        ;;
        --location=*)
            location="${1#*=}"
        ;;
        --help | -h)
            print_help
        ;;
        -* | --*)
            echo "unknown option ${1}"
            print_help
        ;; 
        
    esac
    shift
done

if [ ! -z  "$tag" ] && [ ! -z "$location" ]; then 
    echo "Either release TAG or location of configuration files must be specified" 
    exit 1
fi

cloud_provider=$(kubectl get nodes -ojson | jq -r '.items[0] | .spec | .providerID ' | cut -d':' -f1)
os=$(kubectl get nodes -ojson | jq -r '.items[0] | .status| .nodeInfo| .osImage')
post_process=""

if [ "$cloud_provider" = "azure" ]; then
    echo "on Azure"
    overlay=azure-aks
elif [ "$cloud_provider" = "aws" ]; then
    echo "on Amazon"
    overlay=amazon-eks-custom
elif [ "$cloud_provider" = "gce" ]; then
    if [[ $os =~ Ubuntu* ]]; then 
        echo "on GKE Ubuntu"
        overlay=gke
    elif [[ $os =~ Google ]]; then 
        echo "On Container-Optimized OS from Google"
        overlay=gke-gvisor
    fi
elif [ "$cloud_provider" = "k3s" ]; then
    echo "On K3s"
    overlay=k3s
    post_process="sudo systemctl restart k3s"
else
    echo "If you are runnign K3s make sure change kubectl config by runnig the followinf command:"
    echo "  export KUBECONFIG=/etc/rancher/k3s/k3s.yaml"
    echo "If not, you are running Kontain-unsuported cluster provider"
    exit 1
fi


if [ -z  "$tag" ]; then
    tag=$(curl -L -s https://raw.githubusercontent.com/kontainapp/km/current/km-releases/current_release.txt)
fi

if [ ! -z "$location" ]; then
    location=${location}/overlays/${overlay}
else 
    location="https://github.com/kontainapp/km/cloud/k8s/deploy/kontain-deploy/overlays/${overlay}?ref=${tag}"
fi

echo "TAG = $tag"
echo "LOCATION = $location"

kubectl apply -k $location

${post_process}