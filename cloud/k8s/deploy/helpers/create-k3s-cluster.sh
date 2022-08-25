# Copyright 2021 Kontain
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

print_help() {
    echo "usage: $0  [options] prefix"
    echo "Creates AKS cluster with the name <prefix>-aks-cluster. All other associated recource names are prefixes with <prefix> "
    echo ""
    echo "Prerequisites:"
    echo "  AZURE CLI"
    echo ""
    echo "-h,--help print this help"
    echo "--tenant AZURE tenant ID"
    echo "--app-id AZURE app id"
    echo "--password AZURE secret or password"
    echo "--region Sets aws region. Default to us-west-1"
    echo "--cleanup Instructs script to delete cluster and all related resourses "
    exit 0
}
main() {
    
    curl -sfL https://get.k3s.io | INSTALL_K3S_EXEC="--no-deploy traefik --disable servicelb" INSTALL_K3S_VERSION="v1.24.3+k3s1" sh -s - --write-kubeconfig-mode 666
    # sudo systemctl enable --now k3s
    # sudo systemctl start k3s

    echo waiting for k3s to become active
    until systemctl is-active k3s; do echo -n "."; done

    echo waiting for k3s server to establish connection
    until netstat -nat |grep 644|grep ESTABLISHED > /dev/null; do echo -n ".";done
    
    sleep 20

    export KUBECONFIG=/etc/rancher/k3s/k3s.yaml
    kubectl wait --for=condition=Ready pods --all -n kube-system

    #install node 
    # TOKEN=$(sudo cat /var/lib/rancher/k3s/server/node-token)
    # curl -sfL https://get.k3s.io | K3S_URL="https://127.0.0.1:6443" K3S_TOKEN="$TOKEN" sh -

}

do_cleanup() {

    # sudo systemctl stop k3s-agent.service
    sudo systemctl stop k3s.service

    # /usr/local/bin/k3s-agent-uninstall.sh
    /usr/local/bin/k3s-uninstall.sh

    echo "waiting for all precesses to terminate and sockets to free"
    until ! netstat -nat |grep 644|grep TIME_WAIT > /dev/null; do echo -n "";done

}

arg_count=$#

for arg in "$@"
do
   case "$arg" in
        --cleanup)
            cleanup='yes'
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

if [ ! -z $cleanup ] && [ $arg_count == 1 ]; then
    do_cleanup
    exit
fi

main
