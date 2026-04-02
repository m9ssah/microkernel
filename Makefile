.PHONY: all clean
FLAGS = -Wall -g -Iinclude

all: bin/kernel bin/worker bin/param_server bin/monitor

bin/kernel: kernel/kernel.c common/net.c include/net.h
	mkdir -p bin
	gcc ${FLAGS} -o bin/microkernel kernel/kernel.c common/net.c

bin/worker: workers/worker.c common/net.c include/net.h
	mkdir -p bin
	gcc ${FLAGS} -o bin/worker workers/worker.c common/net.c -lm

bin/param_server: param_server/param_server.c common/net.c include/net.h
	mkdir -p bin
	gcc ${FLAGS} -o bin/param_server param_server/param_server.c common/net.c

bin/monitor: monitor/monitor.c common/net.c include/net.h
	mkdir -p bin
	gcc ${FLAGS} -o bin/monitor monitor/monitor.c common/net.c

clean:
	rm -rf bin
	