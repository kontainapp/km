CFLAGS = -Wall -ggdb -O2

HDR = km.h km_hcalls.h x86_cpu.h
SRC = load_elf.c km_cpu_init.c km_main.c km_vcpu_run.c km_hcalls.c

OBJ = $(SRC:.c=.o)

# colors for nice color in output
RED := \033[31m
GREEN := \033[32m
YELLOW := \033[33m
CYAN := \033[36m
NOCOLOR := \033[0m

#
# Default target (should be first, to support simply 'make' invoking 'make help')
#
# This target ("help") scans Makefile for '##' in targets and prints a summary
# Note - used awk to print (instead of echo) so escaping/coloring is platform independed
.PHONY: help
help:  ## Prints help on 'make' targets
	@awk 'BEGIN {FS = ":.*##"; printf "\nUsage:\n  make $(CYAN)<target>$(NOCOLOR)\n" } \
    /^[a-zA-Z0-9_-]+:.*?##/ { printf "  $(CYAN)%-15s$(NOCOLOR) %s\n", $$1, $$2 } \
	/^##@/ { printf "\n\033[1m%s$(NOCOLOR)\n", substr($$0, 5) } ' \
	$(MAKEFILE_LIST)
	@echo ""


km: $(OBJ)  ## build the 'km' VMM
	gcc $(OBJ) -lelf -o km

$(OBJ): $(HDR)

clean:  ## removes .o and other artifacts
	rm -f *.o km hello hello_html

hello:	hello.c  ## builds 'hello world' example (load a unikernel and print hello via hypercall)
	gcc -c -O2 hello.c
	ld -T km.ld hello.o -o hello

hello_html: hello_html.c runtime.c  ## builds 'hello world" HTML example - run with ``km hello_world'' in one window, then from browser go to http://127.0.0.1:8002.
	gcc -c -O2 hello_html.c
	gcc -c -O2 runtime.c
	ld -T km.ld hello_html.o runtime.o -o hello_html

test: km hello hello_html ## Builds all and runs a simple test
	# TBD - make return from km compliant with "success" :-)
	-./km hello
	./km hello_html &
	curl -s http://127.0.0.1:8002 | grep -q  "I'm here"
	@echo Success