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
	gcc ${FLAGS} -o bin/param_server param_server/param_server.c common/net.c -lm

bin/monitor: monitor/monitor.c common/net.c include/net.h
	mkdir -p bin
	gcc ${FLAGS} -o bin/monitor monitor/monitor.c common/net.c

shards:
	python3 gen_shards.py

firmware:
	$(MAKE) -C firmware

check-fastmath:
	mkdir -p bin
	gcc ${FLAGS} -O2 -o bin/test_fastmath tests/test_fastmath.c firmware/fastmath.c -lm
	./bin/test_fastmath

check-firmware:
	bash tests/test_firmware_fault.sh
	bash tests/test_firmware_gradient.sh

check: check-fastmath check-firmware

clean:
	rm -rf bin
	$(MAKE) -C firmware clean
