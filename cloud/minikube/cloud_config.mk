#
# Copyright 2021 Kontain Inc.
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
#
#
# config file. Used  from both makefiles and bash scripts, so no spaces before/after '='
#

CLOUD_LOCATION=localhost

# Container registry name, usually passed to misc. commands
REGISTRY_NAME=kontainapp

# Server name for the registry, usualy a part of container tag needed for the push.
# Can be received with
REGISTRY=${REGISTRY_NAME}

# used in misc. messages
REGISTRY_AUTH_EXAMPLE="docker login"
