TOP := $(shell git rev-parse --show-cdup)

OBJS=dweb.o dwebsvr.o cpuid.o

override CFLAGS += -g -O2 -Wstringop-overflow=0
override LDFLAGS += -g

KM_LDFLAGS=${LDFLAGS} -static --gc-sections -nostdlib -T ${TOP}km.ld -L ${TOP}build/runtime -lruntime

all: dweb dweb.km

dweb: $(OBJS)
	$(CC) -o $@ $^ -pthread -std=gnu99

dweb.km: $(OBJS)
	$(LD) -o $@ $^  ${KM_LDFLAGS} ; chmod a-x $@

.PHONY: clean
clean:
	rm -f *.o dweb dweb.km
