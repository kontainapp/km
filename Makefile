CFLAGS = -Wall -ggdb -O2

HDR = km.h km_hcalls.h x86_cpu.h
SRC = load_elf.c km_cpu_init.c km_main.c km_vcpu_run.c km_hcalls.c

OBJ = $(SRC:.c=.o)

km: $(OBJ)
	gcc $(OBJ) -lelf -o km

$(OBJ): $(HDR)

clean:
	rm -f *.o a.out km

a.out:	hello.c
	gcc -c -O2 hello.c
	ld -T km.ld hello.o
