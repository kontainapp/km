
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
	${TOP}/tools/kontain-gcc $(CFLAGS) -o $@ $^

PAUSE_CONTAINER = kontain/pause-km

distro: $(BIN_KM)
	docker build -t $(PAUSE_CONTAINER) .

clean:
	rm -rf \
		$(BIN) \
		$(BIN_KM) \
		$(OBJS)
	-docker rmi $(PAUSE_CONTAINER)
