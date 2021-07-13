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
#
# Dockerfile for tensor flow inferencing test app.
# We have to use our own tensorflow build to avoud usage of non-POSIX APIs.
# Copy the .whl file from ../../tools/hashicorp/build_tensorflow for Docker to pick it up.

FROM fedora:32 as app
ARG UID=1001
ARG GID=117
ARG USERNAME=appuser
RUN groupadd -f -g $GID $USERNAME && useradd -u $UID -g $GID -d /home/$USERNAME $USERNAME
RUN dnf install -y pip
USER $USERNAME
WORKDIR /home/$USERNAME
COPY app.py requirements.txt tensorflow-2.4.1-cp38-cp38-linux_x86_64.whl /home/$USERNAME/
RUN pip install --user -r requirements.txt tensorflow-2.4.1-cp38-cp38-linux_x86_64.whl
RUN rm requirements.txt tensorflow-2.4.1-cp38-cp38-linux_x86_64.whl

FROM kontainapp/runenv-dynamic-python
ARG UID=1001
ARG GID=117
ARG USERNAME=appuser
COPY --from=app /etc/passwd /etc/
COPY --from=app /etc/pki /etc/pki/
COPY --from=app /etc/ssl /etc/ssl/
COPY --from=app --chown=$UID:$GID /tmp /tmp/
USER appuser
ENV HOME /home/$USERNAME
WORKDIR $HOME
COPY --chown=$UID:$GID run.sh run_snap.sh /
COPY --from=app --chown=$UID:$GID $HOME $HOME/
CMD [ "/usr/local/bin/python", "app.py" ]
