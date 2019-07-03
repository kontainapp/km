TOP := $(shell git rev-parse --show-cdup)

OBJS=dweb.o dwebsvr.o cpuid.o

override CFLAGS += -g -O2 -Wstringop-overflow=0 -std=gnu99 -U_FORTIFY_SOURCE

all: dweb dweb.km

dweb: $(OBJS)
	$(CC) $(CLFAGS) -pthread  -o $@ $^

KM_LDFLAGS := -specs=${TOP}gcc-km.spec -L ${TOP}build/runtime
dweb.km: $(OBJS)
	$(CC) -static -no-pie $(CLFAGS) -pthread  -o $@ $^ ${KM_LDFLAGS} && chmod a-x $@

.PHONY: clean
clean:
	rm -f *.o dweb dweb.km
