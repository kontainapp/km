#!/usr/bin/env bash
# Copyright 2021 Kontain
#
# Derived from kata-container install.sh:
# Copyright (c) 2019 Intel Corporation
#
# SPDX-License-Identifier: Apache-2.0
#

set -x
set -o errexit
set -o pipefail
set -o nounset

PATH=$PATH:/opt/kontain-artifacts/tools

crio_drop_in_conf_dir="/etc/crio/crio.conf.d/"
crio_drop_in_conf_file="${crio_drop_in_conf_dir}/99-kontain-deploy"
containerd_conf_file="/etc/containerd/config.toml"
containerd_conf_file_backup="${containerd_conf_file}.bak"

# If we fail for any reason a message will be displayed
die() {
        msg="$*"
        echo "ERROR: $msg" >&2
        exit 1
}

function print_usage() {
	echo "Usage: $0 [install/cleanup/reset]"
}

function get_container_runtime() {

	local runtime=$(kubectl get node $NODE_NAME -o jsonpath='{.status.nodeInfo.containerRuntimeVersion}')
	if [ "$?" -ne 0 ]; then
                die "invalid node name"
	fi
	if echo "$runtime" | grep -qE 'containerd.*-k3s'; then
		if systemctl is-active --quiet k3s-agent; then
			echo "k3s-agent"
		else
			echo "k3s"
		fi
	else
		echo "$runtime" | awk -F '[:]' '{print $1}'
	fi
}

function install_artifacts() {
	echo "copying kontain artifacts onto host"
	mkdir -p /opt/kontain/bin
	cp -a /opt/kontain-artifacts/bin/* /opt/kontain/bin/
	chmod +x /opt/kontain/bin/*
	mkdir -p /usr/local/bin
	cp -a /opt/kontain-artifacts/shim/containerd-shim-krun-v2 /usr/local/bin/containerd-shim-krun-v2
	chmod +x /usr/local/bin/containerd-shim-krun-v2
}

function configure_cri_runtime() {

	case $1 in
	crio)
		configure_crio
		;;
	containerd | k3s | k3s-agent)
		configure_containerd
		;;
	esac

        [ -c /host-dev/kvm ] && chmod 666 /host-dev/kvm
        mkdir -p /etc/udev/rules.d
        echo 'KERNEL=="kvm", GROUP="kvm", MODE="0666"' | tee /etc/udev/rules.d/99-perm.rules
        udevadm control --reload-rules && udevadm trigger

	systemctl daemon-reload
	systemctl restart "$1"
}

function configure_crio_runtime() {
	cat <<EOT | tee -a "$crio_drop_in_conf_file"
[crio.runtime.runtimes.krun]
runtime_path = "/opt/kontain/bin/krun"
runtime_type = "oci"
runtime_root = "/run/krun"
EOT
}

function configure_crio() {
	# Configure crio to use Kontain:
	echo "Add Kontain as a supported runtime for CRIO:"

	# As we don't touch the original configuration file in any way,
	# let's just ensure we remove any exist configuration from a
	# previous deployment.
	mkdir -p "$crio_drop_in_conf_dir"
	rm -f "$crio_drop_in_conf_file"
	touch "$crio_drop_in_conf_file"

	configure_crio_runtime
}

function configure_containerd_runtime() {
	local runtime="krun"
	local configuration="configuration"
	local pluginid=cri
	if grep -q "version = 2\>" $containerd_conf_file; then
		pluginid=\"io.containerd.grpc.v1.cri\"
	fi

	local runtime_table="plugins.${pluginid}.containerd.runtimes.$runtime"
	local runtime_type="io.containerd.$runtime.v2"
	local options_table="$runtime_table.options"
	local config_path=""
	if grep -q "\[$runtime_table\]" $containerd_conf_file; then
		echo "Configuration exists for $runtime_table, overwriting"
		sed -i "/\[$runtime_table\]/,+1s#runtime_type.*#runtime_type = \"${runtime_type}\"#" $containerd_conf_file
	else
		cat <<EOT | tee -a "$containerd_conf_file"
[$runtime_table]
  runtime_type = "${runtime_type}"
  privileged_without_host_devices = true
  pod_annotations = ["app.kontain.*"]
EOT
	fi
}

function configure_containerd() {
	# Configure containerd to use Kata:
	echo "Add Kata Containers as a supported runtime for containerd"

	mkdir -p /etc/containerd/

	if [ -f "$containerd_conf_file" ]; then
		# backup the config.toml only if a backup doesn't already exist (don't override original)
		cp -n "$containerd_conf_file" "$containerd_conf_file_backup"
	fi

	# Add default Kata runtime configuration
	configure_containerd_runtime
}

function remove_artifacts() {
	echo "deleting kontain artifacts"
	rm -rf /opt/kontain/
	rm -rf /usr/local/bin/containerd-shim-krun-v2
}

function cleanup_cri_runtime() {
	case $1 in
	crio)
		cleanup_crio
		;;
	containerd | k3s | k3s-agent)
		cleanup_containerd
		;;
	esac

}

function cleanup_crio() {
	rm $crio_drop_in_conf_file
}

function cleanup_containerd() {
	rm -f $containerd_conf_file
	if [ -f "$containerd_conf_file_backup" ]; then
		mv "$containerd_conf_file_backup" "$containerd_conf_file"
	fi
}

function reset_runtime() {
	kubectl label node "$NODE_NAME" kontain.app/kontain-runtime-
	systemctl daemon-reload
	systemctl restart "$1"
	if [ "$1" == "crio" ] || [ "$1" == "containerd" ]; then
		systemctl restart kubelet
	fi
}

function main() {
	# script requires that user is root
	euid=$(id -u)
	if [[ $euid -ne 0 ]]; then
	   die  "This script must be run as root"
	fi

	echo "Kontain Node Installation. NODE_NAME=${NODE_NAME}"

	runtime=$(get_container_runtime)

	# CRI-O isn't consistent with the naming -- let's use crio to match the service file
	if [ "$runtime" == "cri-o" ]; then
		runtime="crio"
	elif [ "$runtime" == "k3s" ] || [ "$runtime" == "k3s-agent" ]; then
		containerd_conf_tmpl_file="${containerd_conf_file}.tmpl"
		if [ ! -f "$containerd_conf_tmpl_file" ]; then
			cp "$containerd_conf_file" "$containerd_conf_tmpl_file"
		fi

		containerd_conf_file="${containerd_conf_tmpl_file}"
		containerd_conf_file_backup="${containerd_conf_file}.bak"
	fi

	action=${1:-}
	if [ -z "$action" ]; then
		print_usage
		die "invalid arguments"
	fi

	# only install / remove / update if we are dealing with CRIO or containerd
	if [[ "$runtime" =~ ^(crio|containerd|k3s|k3s-agent)$ ]]; then

		case "$action" in
		install)

			install_artifacts
			configure_cri_runtime "$runtime"
			kubectl label node "$NODE_NAME" --overwrite kontain.app/kontain-runtime=true
			;;
		cleanup)
			cleanup_cri_runtime "$runtime"
			kubectl label node "$NODE_NAME" --overwrite kontain.app/kontain-runtime=cleanup
			remove_artifacts
			;;
		reset)
			reset_runtime $runtime
			;;
		*)
			echo invalid arguments
			print_usage
			;;
		esac
	fi

	#It is assumed this script will be called as a daemonset. As a result, do
        # not return, otherwise the daemon will restart and rexecute the script
	sleep infinity
}

main "$@"
