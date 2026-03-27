FLAGS = -Wall -g -Iinclude
.PHONY: all clean

all: kernel worker param_server 

kernel: kernel/kernel.c
	gcc ${FLAGS} -o kernel kernel/kernel.c

worker: workers/worker.c
	gcc ${FLAGS} -o workers workers/worker.c

param_server: 
	gcc ${FLAGS} -o param_server workers/param_server.c

clean:
	rm -r kernel worker param_server
