TOP := $(shell git rev-parse --show-toplevel)

OBJS=dweb.o dwebsvr.o cpuid.o

override CFLAGS += -g -O2 -Wstringop-overflow=0 -std=gnu99 -U_FORTIFY_SOURCE

all: dweb dweb.km

dweb: $(OBJS)
	$(CC) -static $(CLFAGS) -pthread  -o $@ $^

dweb.km: $(OBJS)
	${TOP}/tools/bin/kontain-gcc -static -no-pie $(CLFAGS) -pthread  -o $@ $^ && chmod a-x $@

.PHONY: clean
clean:
	rm -f *.o dweb dweb.km
