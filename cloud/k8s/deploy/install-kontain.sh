#!/usr/bin/env bash
# Copyright 2021 Kontain
#
# Derived from kata-container install.sh:
# Copyright (c) 2019 Intel Corporation
#
# SPDX-License-Identifier: Apache-2.0
#

#
# This script is meant to be run remotely to determine how to install 
# the Kontain Runtime on a kubernetes cluster.

set -o errexit
set -o pipefail
set -o nounset


function get_container_runtime() {
	local node=$(kubectl get nodes --no-headers | head --lines=1 | awk '{print $1}')
	local runtime=$(kubectl get node "${node}" -o jsonpath='{.status.nodeInfo.containerRuntimeVersion}')
        if echo "$runtime" | grep -qE 'containerd.*-k3s'; then
                if systemctl is-active --quiet k3s-agent; then
                        echo "k3s-agent"
			return 0
                else
                        echo "k3s"
			return 0
                fi
        fi
	runtime=$(echo "$runtime" | awk -F '[:]' '{print $1}')
	if [ "${runtime}" == "cri-o" ]; then
		runtime="crio"
	fi
	echo "${runtime}"
	return 0
}

function main() {
	local runtime=$(get_container_runtime)
	case "${runtime}" in
	containerd)
		echo "containerd"
		;;
	crio)
		echo "crio"
		;;
	*) 
		echo "Unsupported container runtime manager ${runtime}"
		return 1
		;;
	esac
}

main "$@"
