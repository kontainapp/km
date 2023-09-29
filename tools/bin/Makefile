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

TOP := $(shell git rev-parse --show-toplevel)

include ${TOP}/make/actions.mk

all coverage: kontain-gcc | ${KM_OPT_BIN}
	cp kontain-gcc ${KM_OPT_BIN}
	cp kontain-ar ${KM_OPT_BIN}
	cp kontain-strip ${KM_OPT_BIN}
	ln -sf kontain-gcc ${KM_OPT_BIN}/kontain-g++

clean:
	rm -f ${KM_OPT_BIN}/kontain-gcc
	rm -f ${KM_OPT_BIN}/kontain-ar
	rm -f ${KM_OPT_BIN}/kontain-strip
	rm -f ${KM_OPT_BIN}/kontain-g++