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
            char read_fd_str[16];
            char write_fd_str[16];
            char worker_id_str[16];

            close(k2w[1]);
            close(w2k[0]);

            snprintf(read_fd_str, sizeof(read_fd_str), "%d", k2w[0]);
            snprintf(write_fd_str, sizeof(write_fd_str), "%d", w2k[1]);
            snprintf(worker_id_str, sizeof(worker_id_str), "%u", worker_id);

            execl("./workers", "worker", read_fd_str, write_fd_str, worker_id_str, (char *)NULL);
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

            char kernel_read_fd_str[16];
            char kernel_write_fd_str[16];
            char num_workers_str[16];

            char worker_read_fd_str[NUM_WORKERS][16];
            char worker_write_fd_str[NUM_WORKERS][16];
            char *argv[5 + (NUM_WORKERS * 2)];
            int arg_idx = 0;

            snprintf(kernel_read_fd_str, sizeof(kernel_read_fd_str), "%d", k2p[0]);
            snprintf(kernel_write_fd_str, sizeof(kernel_write_fd_str), "%d", p2k[1]);
            snprintf(num_workers_str, sizeof(num_workers_str), "%d", NUM_WORKERS);

            argv[arg_idx++] = (char *)"param_server";
            argv[arg_idx++] = kernel_read_fd_str;
            argv[arg_idx++] = kernel_write_fd_str;
            argv[arg_idx++] = num_workers_str;

            for (int i = 0; i < NUM_WORKERS; i++)
            {
                  snprintf(worker_read_fd_str[i], sizeof(worker_read_fd_str[i]), "%d", processes[i].read_fd);
                  snprintf(worker_write_fd_str[i], sizeof(worker_write_fd_str[i]), "%d", processes[i].write_fd);
                  argv[arg_idx++] = worker_read_fd_str[i];
                  argv[arg_idx++] = worker_write_fd_str[i];
            }

            argv[arg_idx] = NULL;

            execv("./workers", argv);
            perror("execv");
            exit(1);
      }

      // parent process
      close(k2p[0]);
      close(p2k[1]);

      processes[nprocesses].process_id = SERVICE_MODEL;
      processes[nprocesses].read_fd = p2k[0];
      processes[nprocesses].write_fd = k2p[1];
      processes[nprocesses].pid = pid;
      nprocesses++;
}

static void register_processes(void)
{
      for (int i = 0; i < nprocesses; i++)
      {
            Message msg;
            if (recv_message(processes[i].read_fd, &msg) < 0)
            {
                  fprintf(stderr, "[kernel %u] ERROR: failed to read OP_REGISTER\n", processes[i].process_id);
                  exit(1);
            }
            if (msg.header.opcode != OP_REGISTER)
            {
                  fprintf(stderr, "[kernel] ERROR: expected OP_REGISTER, got %u\n", msg.header.opcode);
                  exit(1);
            }

            RegisterAckPayload ack;
            if (msg.header.payload_size >= sizeof(RegisterPayload)) // precaution if payload is too small, just set servicetype to 0
            {
                  RegisterPayload *reg = (RegisterPayload *)msg.payload;
                  ack.servicetype = reg->servicetype;
            }
            else
                  ack.servicetype = 0;
            ack.assigned_id = processes[i].process_id;

            if (send_message(processes[i].write_fd, SERVICE_KERNEL, processes[i].process_id, OP_REGISTER_ACK, &ack, sizeof(ack)) < 0)
            {
                  fprintf(stderr, "[kernel %u] ERROR: failed to send OP_REGISTER_ACK\n", processes[i].process_id);
                  exit(1);
            }
            fprintf(stderr, "[kernel] registered with process: %u, pid: %d\n", processes[i].process_id, processes[i].pid);
      }
}

static void wait_for_children(void)
{
      for (int i = 0; i < nprocesses; i++)
      {
            int status;
            waitpid(processes[i].pid, &status, 0);
            fprintf(stderr, "[kernel] pid %d exited with status %d\n", processes[i].pid, WEXITSTATUS(status));
      }
}

static void close_pipes(void)
{
      for (int i = 0; i < nprocesses; i++)
      {
            close(processes[i].write_fd);
            close(processes[i].read_fd);
      }
}

int main(void)
{
      for (uint32_t i = 1; i <= NUM_WORKERS; i++)
            fork_worker(i);
      fork_param_server();

      register_processes();
      wait_for_children();
      close_pipes();

      fprintf(stderr, "[kernel] all processes terminated, exiting\n");
      return 0;
}