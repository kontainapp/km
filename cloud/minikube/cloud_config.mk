# Copyright © 2018 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
#
# config file. Used  from both makefiles and bash scripts, so no spaces before/after '='
#

CLOUD_LOCATION=localhost

# Container registry name, usually passed to misc. commands
REGISTRY_NAME=msterinkontain

# Server name for the registry, usualy a part of container tag needed for the push.
# Can be received with
REGISTRY=${REGISTRY_NAME}

# used in misc. messages
REGISTRY_AUTH_EXAMPLE="docker login"
