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

static void register(void)
{
      for (int i = 0; i < nprocesses; i++)
      {
            Message msg;
            if (recv_message(processes[i].readfd, &msg) < 0)
            {
                  fprintf(stderr, "[kernel %u] ERROR: failed to read OP_REGISTER\n", processes[i].process_id)
                      exit(1);
            }
            if (msg.header.opcode != OP_REGISTER)
            {
                  fprintf(stderr, "[kernel] ERROR: expected OP_REGISTER, got %u\n", msg.header.opcode);
                  exit(1);
            }

            RegisterAckPayload ack;
            ack.servicetype = msg.header.src_id;
            ack.assigned_id = processes[i].process_id;

            if (send_message(processes[i].write_fd, processes[i].process_id, SERVICE_KERNEL, OP_REGISTER_ACK, &ack, sizeof(ack)) < 0)
            {
                  fprintf(stderr, "[kernel %u] ERROR: failed to send OP_REGISTER_ACK\n", processes[i].process_id);
                  exit(1);
            }
            fprintf(stderr, "[kernel] registered with process: %u, pid: %d\n", processes[i].process_id, processes[i].pid);
      }
}

static int find_write_fd(uint32_t process_id)
{
      for (int i = 0; i < nprocesses; i++)
      {
            if (processes[i].process_id == process_id)
                  return processes[i].write_fd;
      }
      return -1;
}

static void run_rounds(void)
{
      for (int round = 0; round < NUM_ROUNDS; round++)
      {
            fprintf(stderr, "[kernel] starting round %d\n" round);

            RoundStartPayload rs;
            rs.round_id = round;

            for (int i = 1; i < nprocesses; i++) // start at 1 to skip param_server
            {
                  if (send_message(processes[i].write_fd, processes[i].process_id, SERVICE_KERNEL, OP_ROUND_START, &rs, sizeof(rs)) < 0)
                  {
                        fprintf(stderr, "[kernel %u] ERROR: failed to send OP_ROUND_START\n" processes[i].process_id);
                        exit(1);
                  }
            }

            // collect grads and send weights to param server
            for (int i = 1; i <= NUM_WORKERS; i++)
            {
                  Message msg;
                  if (recv_message(processes[i].write_fd, &msg) < 0)
                  {
                        fprintf(stderr, "[kernel %u] ERROR: failed to read OP_SUBMIT_GRADIENT\n", processes[i].process_id);
                        continue;
                  }

                  msg.header.dest_id = processes[0].process_id;
                  if (send_message(processes[i].write_fd, processes[i].processed_id, SERVICE_KERNEL, msg_header.opcode, msg.payload, msg.header.payload_size) < 0)
                  {
                        fprintf(stderr, "[kernel %u] ERROR: failed to forward gradient to param server\n", processes[i].process_id);
                        break;
                  }
            }

            Message resp;
            if (recv_message(processes[0].write_fd, &resp) < 0)
            {
                  fprintf(stderr, "[kernel] ERROR: failed to receive updated weights from param server\n");
                  continue;
            }

            for (int i = 1; i < nprocesses, i++)
            {
                  if (send_message(processes[i].write_fd, processes[i].process_id, SERVICE_KERNEL, resp.header.opcode, resp.payload, resp.header.payload_size) < 0)
                  {
                        fprintf(stderr, "[kernel %u] ERROR: failed to send updated weights to worker\n", processes[i].process_id);
                        continue;
                  }
            }
      }
}

static void terminate(void)
{
      for (int i = 0; i < nprocesses; i++)
      {
            if (send_message(processes[i].write_fd, processes[i].process_id, SERVICE_KERNEL, OP_TERMINATE, NULL, 0) < 0)
            {
                  fprintf(stderr, "[kernel %u] ERROR: failed to send OP_TERMINATE\n", processes[i].process_id);
                  exit(1);
            }
            close(processes[i].write_fd);
      }

      for (int i = 0; i < nprocesses; i++)
      {
            int status;
            waitpid(processes[i].pid, &status, 0);
            fprintf(stderr, "[kernel] pid %d exited with status%d\n", processes[i].pid, WEXITSTATUS(status));
            close(processes[i].read_fd);
      }
}

int main(void)
{
      fork_param_server();
      for (uint32_t i = 0; i < NUM_WORKERS; i++)
            fork_worker(i);

      register();
      run_rounds();
      terminate();

      fprintf(stderr, "[kernel] all processes terminated, exiting\n");
      return 0;
}