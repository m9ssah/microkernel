#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <stdint.h>

#include "../include/protocol.h"
#include "../include/payloads.h"
#include "../include/net.h"

#define NUM_WORKERS 3    // TODO: change as needed
#define NUM_ROUNDS  5    // TODO: change as needed
#define NUM_PROCESSES (1 + NUM_WORKERS)

typedef struct {
      uint32_t process_id;
      int read_fd;
      int write_fd;
      pid_t pid;
} ProcessEntry;

static ProcessEntry processes[NUM_PROCESSES]; 
static int nprocesses = 0;
