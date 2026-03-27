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

#define NUM_WORKERS 3 // TODO: change as needed
#define NUM_ROUNDS 5  // TODO: change as needed
#define NUM_PROCESSES (1 + NUM_WORKERS)

typedef struct
{
      uint32_t process_id;
      int read_fd;
      int write_fd;
      pid_t pid;
} ProcessEntry;

static ProcessEntry processes[NUM_PROCESSES];
static int nprocesses = 0;

static void fork_param_server(void)
{
      int k2p[2];
      int p2k[2];

      if (pipe(k2p) < 0 || pipe(p2k) < 0)
      {
            perror("pipe");
            exit(1);
      }

      pid_t pid = fork();

      if (pid < 0)
      {
            perror("fork");
            exit(1);
      }

      if (pid == 0) // child process
      {
            close(k2p[1]);
            close(p2k[0]);

            if (dup2(k2p[0], STDIN_FILENO) == -1)
            {
                  perror("dup2");
                  exit(1);
            };

            if (dup2(p2k[1], STDOUT_FILENO) == -1)
            {
                  perror("dup2");
                  exit(1);
            };

            close(k2p[0]);
            close(p2k[1]);

            execl("./workers", "param_server", NULL);
            perror("execl");
            exit(1);
      }

      // parent process
      close(k2p[0]);
      close(p2k[1]);

      processes[nprocesses].process_id = SERVICE_SCHEDULER;
      processes[nprocesses].read_fd = p2k[0];
      processes[nprocesses].write_fd = k2p[1];
      processes[nprocesses].pid = pid;
      nprocesses++;
}

static void fork_worker(uint32_t worker_id)
{
      int k2w[2];
      int w2k[2];

      if (pipe(k2w) < 0 || pipe(w2k) < 0)
      {
            perror("pipe");
            exit(1);
      }

      pid_t pid = fork();

      if (pid < 0)
      {
            perror("fork");
            exit(1);
      }

      if (pid == 0) // child process
      {
            close(k2w[1]);
            close(w2k[0]);

            if (dup2(k2w[0], STDIN_FILENO) == -1)
            {
                  perror("dup2");
                  exit(1);
            };

            if (dup2(w2k[1], STDOUT_FILENO) == -1)
            {
                  perror("dup2");
                  exit(1);
            }

            close(k2w[0]);
            close(w2k[1]);

            execl("./workers", "worker", NULL);
            perror("execl");
            exit(1);
      }

      // parent process
      close(k2w[0]);
      close(w2k[1]);

      processes[nprocesses].process_id = worker_id;
      processes[nprocesses].read_fd = w2k[0];
      processes[nprocesses].write_fd = k2w[1];
      processes[nprocesses].pid = pid;
      nprocesses++;
}

int main(void)
{
      fork_param_server();
      for (uint32_t i = 0; i < NUM_WORKERS; i++)
            fork_worker(i);

      return 0;
}