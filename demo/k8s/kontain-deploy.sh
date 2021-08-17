#
# Copyright 2021 Kontain Inc
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# main program for kontain-deploy DeamonSet

die() {
        msg="$*"
        echo "ERROR: $msg" >&2
        exit 1
}

function print_usage() {
	echo "Usage: $0 [install/cleanup/reset]"
}

function install() {
  # Copy kontain binaries to /opt/kontain
  echo "copy kontain artifacts onto host"
  cp -a /kontain-atrifacts/opt/kontain /opt/kontain

  # Add crio configuration
  echo "configure crio to support kontain runtime"
  cat <<EOT >> /etc/crio/crio.conf.d/99-crio-kontain.conf
# Kontain Containers
[crio.runtime.runtimes.krun]
runtime_path = "/opt/kontain/bin/krun"
runtime_type = "oci"
runtime_root = "/run/krun"
EOT
}

function uninstall() {
  echo "Remove crio config for kontain runtime"
  rm /etc/crio/crio.conf.d/99-crio-kontain.conf

  echo "Remove kontain artifacts"
  rm -rf /opt/kontain
}

function main() {
  euid=$(id -u)
  if [ ${euid} -ne 0 ]; then
    die "must be run as root"
  fi

  action=${1:-}
  if [ -z "$action" ]; then
    print_usage
    die "invalid arguments"
  fi

  case "$action" in
  install) 
    install
    ;;
  cleanup)
    uninstall
    ;;
  reset) ;;
  esac
  sleep infinity
}

main "$@"
