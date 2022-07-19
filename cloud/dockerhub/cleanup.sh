#!/bin/bash
#
# Copyright 2022 Kontain Inc
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
# Creates a release kontain.tar.gz for uploading to github. To unpackage, 'tar -C /opt/kontain -xvf kontain.tar.gz'
#
set -e
[ "$TRACE" ] && set -x

HUB_URL="https://hub.docker.com"
REGISTRY_URL="https://registry.hub.docker.com"
REGISTRY_AUTH_URL="https://auth.docker.io"

LATEST_SHA=$(git log -1 --format=format:"%H" latest)

# docker hub credentials
ORG=kontainapp
UNAME="gnode1"
UPASS="71300b3f-244f-47a9-b535-8cd65249f2bc"  # hub.docker access token

# prompt for docker hub password, because personal token has limited functionality per https://github.com/docker/hub-feedback/issues/2006
read -e -s -p "Enter Dockerhub password:" HUB_PASSWD

# docker requstry credentials
AUTH_SERVICE='registry.docker.io'
AUTH_SCOPE="repository:${UNAME}/${UNAME}:pull"

# aquire tokens
HUB_TOKEN=$(curl -s -H "Content-Type: application/json" -X POST -d '{"username": "'${UNAME}'", "password": "'${HUB_PASSWD}'"}' ${HUB_URL}/v2/users/login/ | jq -r .token)


# list images
IMAGES=$(curl -s -H "Authorization: JWT ${HUB_TOKEN}" "${HUB_URL}/v2/repositories/${ORG}/?page_size=100" | jq -r '.results|.[]|.name')

for i in ${IMAGES}
do
    # get tags for repo
    REGISTRY_TOKEN=$(curl -fsSL "${REGISTRY_AUTH_URL}/token?service=$AUTH_SERVICE&scope=repository:${ORG}/${i}:pull" | jq --raw-output '.token')
    # echo "REGISTRY_TOKEN=${REGISTRY_TOKEN}"
    
    # CMD="curl -fsSL -H \"Authorization: Bearer ${REGISTRY_TOKEN}\" ${REGISTRY_URL}/v2/${ORG}/${i}/tags/list"
    # echo "CMD=${CMD}"

    IMAGE_TAGS=$(curl -fsSL -H "Authorization: Bearer ${REGISTRY_TOKEN}" ${REGISTRY_URL}/v2/${ORG}/${i}/tags/list |jq -r '.tags|.[]')

    echo "IMAGE ${i}"

    for t in ${IMAGE_TAGS}
    do
        if [[ ! ${t} =~ latest|${LATEST_SHA}|v[[:digit:]]?.[[:digit:]]?* ]]; then
            echo "deleting tag ${t}" 
            curl -H "Authorization: JWT ${HUB_TOKEN}" -X DELETE  "https://hub.docker.com/v2/repositories/${ORG}/${i}/tags/${t}/" 
        # else
        #     echo "keep tag ${t}"    
        fi
    done

done
 
