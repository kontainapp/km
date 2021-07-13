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
# We'll use this just to show size/start time for dweb in a docker container
FROM ubuntu

ADD dweb/dweb dweb/index.html dweb/kontain_logo_transparent.png dweb/text.txt /dweb/
ADD dweb/fonts /dweb/fonts/
ADD dweb/js /dweb/js/
ADD dweb/css /dweb/css/
WORKDIR /dweb
