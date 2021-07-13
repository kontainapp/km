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


TOP := $(shell git rev-parse --show-toplevel)

SRCS = pause.c
BIN = pause
BIN_KM = pause.km
OBJS = pause.o

CFLAGS = -Os -Wall -Werror -static

.PHONY: all clean distro

all: $(BIN) $(BIN_KM)

$(OBJS): $(SRCS)
	cc $(CFLAGS) $^ -c

$(BIN): $(OBJS)
	cc $(CFLAGS) -o $@ $^

$(BIN_KM): $(OBJS)
	${TOP}/tools/bin/kontain-gcc $(CFLAGS) -o $@ $^

PAUSE_CONTAINER = kontainapp/pause-km

distro: $(BIN_KM)
	docker build -t $(PAUSE_CONTAINER) .

clean:
	rm -rf \
		$(BIN) \
		$(BIN_KM) \
		$(OBJS)
	-docker rmi $(PAUSE_CONTAINER)
