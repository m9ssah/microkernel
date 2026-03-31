FLAGS = -Wall -g -Iinclude
.PHONY: all clean

all: kernel worker param_server monitor

kernel: kernel/kernel.c common/net.c include/net.h
	gcc ${FLAGS} -o microkernel kernel/kernel.c common/net.c

worker: workers/worker.c common/net.c include/net.h
	gcc ${FLAGS} -o worker workers/worker.c common/net.c

param_server: param_server/param_server.c common/net.c include/net.h
	gcc ${FLAGS} -o param_server param_server/param_server.c common/net.c

monitor: monitor/monitor.c common/net.c include/net.h
	gcc ${FLAGS} -o monitor monitor/monitor.c common/net.c

clean:
	rm -f microkernel worker param_server monitor
