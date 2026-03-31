#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/select.h>
#include <stdint.h>

#include "../include/protocol.h"
#include "../include/payloads.h"
#include "../include/net.h"

#define NUM_WORKERS 3                   // TODO: change as needed
#define NUM_ROUNDS 10
#define NUM_PROCESSES (2 + NUM_WORKERS) // workers + param_server + monitor

typedef struct
{
      uint32_t process_id;
      int read_fd;
      int write_fd;
      pid_t pid;
      int alive;
} ProcessEntry;

static ProcessEntry processes[NUM_PROCESSES];
static int nprocesses = 0;

static int find_proc_index_by_id(uint32_t process_id)
{
    for (int i = 0; i < nprocesses; i++)
    {
        if (processes[i].process_id == process_id)
            return i;
    }
    return -1;
}

static int find_param_server_index(void)
{
    return find_proc_index_by_id(SERVICE_MODEL);
}

static int find_monitor_index(void)
{
    return find_proc_index_by_id(SERVICE_MONITOR);
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

        if (dup2(k2w[0], STDIN_FILENO) < 0 || dup2(w2k[1], STDOUT_FILENO) < 0)
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

      snprintf(worker_id_str, sizeof(worker_id_str), "%u", wworker_id);
      execl("./workers", "/worker", worker_id_str, NULL);
      perror("execl");
      exit(1);

      processes[nprocesses].process_id = worker_id;
      processes[nprocesses].read_fd = w2k[0];
      processes[nprocesses].write_fd = k2w[1];
      processes[nprocesses].pid = pid;
      processes[nprocesses].alive = 1;
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
      processes[nprocesses].alive = 1;
      nprocesses++;
}

static void fork_monitor(void)
{
      int k2m[2];
      int m2k[2];

    if (pipe(k2m) < 0 || pipe(m2k) < 0)
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

    if (pid == 0)
    {
        close(k2m[1]);
        close(m2k[0]);

        if (dup2(k2child[0], STDIN_FILENO) < 0 || dup2(child2k[1], STDOUT_FILENO) < 0)
        {
            perror("dup2");
            exit(1);
        }

        close(k2m[0]);
        close(m2k[1]);

        execl("./workers", "./monitor", (char *)NULL);
        perror("execl");
        exit(1);
    }

    close(k2m[0]);
    close(m2k[1]);

    processes[nprocesses].process_id = SERVICE_MONITOR;
    processes[nprocesses].read_fd = m2k[0];
    processes[nprocesses].write_fd = k2m[1];
    processes[nprocesses].pid = pid;
    processes[nprocesses].alive = 1;
    nprocesses++;
}

static void register_all(void)
{
    for (int i = 0; i < nprocesses; i++)
    {
        Message msg;
        RegisterAckPayload ack;

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

        ack.servicetype = msg.header.src_id;
        ack.assigned_id = processes[i].process_id;

        if (send_message(processes[i].write_fd, SERVICE_KERNEL, processes[i].process_id,
                         OP_REGISTER_ACK, &ack, sizeof(ack)) < 0)
        {
            fprintf(stderr, "[kernel] ERROR: failed to send OP_REGISTER_ACK to %u\n", processes[i].process_id);
            exit(1);
        }

        fprintf(stderr, "[kernel] registered process %u (pid=%d)\n", processes[i].process_id, processes[i].pid);
    }
}

static int count_alive_workers(void)
{
    int alive = 0;
    for (int i = 0; i < NUM_WORKERS; i++)
    {
        if (processes[i].alive)
            alive++;
    }
    return alive;
}

static void forward_monitor_control(int monitor_idx, int param_idx)
{
    fd_set rfds;
    struct timeval tv;
    Message mon_msg;

    FD_ZERO(&rfds);
    FD_SET(processes[monitor_idx].read_fd, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    if (select(processes[monitor_idx].read_fd + 1, &rfds, NULL, NULL, &tv) <= 0)
        return;

    if (recv_message(processes[monitor_idx].read_fd, &mon_msg) < 0)
        return;

    if (mon_msg.header.opcode == OP_QUERY_PROGRESS)
    {
        Message progress;

        if (send_message(processes[param_idx].write_fd, SERVICE_KERNEL, SERVICE_MODEL,
                         OP_QUERY_PROGRESS, NULL, 0) < 0)
            return;

        if (recv_message(processes[param_idx].read_fd, &progress) < 0)
            return;

        if (progress.header.opcode == OP_PROGRESS)
        {
            send_message(processes[monitor_idx].write_fd, SERVICE_KERNEL, SERVICE_MONITOR,
                         OP_PROGRESS, progress.payload, progress.header.payload_size);
        }
    }
    else if (mon_msg.header.opcode == OP_ADJUST_LR)
    {
        send_message(processes[param_idx].write_fd, SERVICE_KERNEL, SERVICE_MODEL,
                     OP_ADJUST_LR, mon_msg.payload, mon_msg.header.payload_size);
    }
    else if (mon_msg.header.opcode == OP_PAUSE)
    {
        Message resume_msg;

        for (int i = 0; i < NUM_WORKERS; i++)
        {
            if (!processes[i].alive)
                continue;

            send_message(processes[i].write_fd, SERVICE_KERNEL, processes[i].process_id,
                         OP_PAUSE, NULL, 0);
        }

        while (1)
        {
            if (recv_message(processes[monitor_idx].read_fd, &resume_msg) < 0)
                break;

            if (resume_msg.header.opcode == OP_RESUME)
            {
                for (int i = 0; i < NUM_WORKERS; i++)
                {
                    if (!processes[i].alive)
                        continue;

                    send_message(processes[i].write_fd, SERVICE_KERNEL, processes[i].process_id,
                                 OP_RESUME, NULL, 0);
                }
                break;
            }
        }
    }
}

static void run_rounds(void)
{
    int param_idx = find_param_server_index();
    int monitor_idx = find_monitor_index();

    if (param_idx < 0 || monitor_idx < 0)
    {
        fprintf(stderr, "[kernel] ERROR: missing param_server or monitor\n");
        exit(1);
    }

    for (int round = 0; round < NUM_ROUNDS; round++)
    {
        Message msg;
        int alive_workers;
        time_t now;
        struct tm *tm_info;

        for (int i = 0; i < NUM_WORKERS; i++)
        {
            if (!processes[i].alive)
                continue;

            RoundStartPayload rs;
            rs.round_id = round;
            if (send_message(processes[i].write_fd, SERVICE_KERNEL, processes[i].process_id,
                             OP_ROUND_START, &rs, sizeof(rs)) < 0)
            {
                processes[i].alive = 0;
            }
        }

        for (int i = 0; i < NUM_WORKERS; i++)
        {
            if (!processes[i].alive)
                continue;

            if (send_message(processes[i].write_fd, SERVICE_KERNEL, processes[i].process_id,
                             OP_HEARTBEAT_PING, NULL, 0) < 0)
            {
                processes[i].alive = 0;
                continue;
            }

            if (recv_message(processes[i].read_fd, &msg) < 0 || msg.header.opcode != OP_HEARTBEAT_PONG)
            {
                processes[i].alive = 0;
            }
        }

        for (int i = 0; i < NUM_WORKERS; i++)
        {
            if (!processes[i].alive)
                continue;

            if (recv_message(processes[i].read_fd, &msg) < 0 || msg.header.opcode != OP_SUBMIT_GRADIENT)
            {
                processes[i].alive = 0;
                continue;
            }

            if (send_message(processes[param_idx].write_fd, processes[i].process_id, SERVICE_MODEL,
                             OP_SUBMIT_GRADIENT, msg.payload, msg.header.payload_size) < 0)
            {
                fprintf(stderr, "[kernel] ERROR: failed to forward gradient to param_server\n");
                break;
            }
        }

        forward_monitor_control(monitor_idx, param_idx);

        if (recv_message(processes[param_idx].read_fd, &msg) < 0)
        {
            fprintf(stderr, "[kernel] ERROR: failed to receive message from param_server\n");
            break;
        }

        if (msg.header.opcode == OP_ROUND_COMPLETE)
        {
            RoundCompletePayload *rc = (RoundCompletePayload *)msg.payload;
            if (rc->status == 1)
            {
                fprintf(stderr, "[kernel] convergence reached at round %u\n", round);
                break;
            }
        }
        else if (msg.header.opcode == OP_WEIGHTS)
        {
            for (int i = 0; i < NUM_WORKERS; i++)
            {
                if (!processes[i].alive)
                    continue;

                send_message(processes[i].write_fd, SERVICE_KERNEL, processes[i].process_id,
                             OP_WEIGHTS, msg.payload, msg.header.payload_size);
            }
        }

        alive_workers = count_alive_workers();
        fprintf(stderr, "[kernel] round=%u alive_workers=%d", round, alive_workers);
    }
}

static void shutdown_all(void)
{
    for (int i = 0; i < nprocesses; i++)
    {
        send_message(processes[i].write_fd, SERVICE_KERNEL, processes[i].process_id,
                     OP_TERMINATE, NULL, 0);
    }

    for (int i = 0; i < nprocesses; i++)
        close(processes[i].write_fd);

    for (int i = 0; i < nprocesses; i++)
    {
        int status;
        waitpid(processes[i].pid, &status, 0);
        if (WIFEXITED(status))
            fprintf(stderr, "[kernel] pid %d exited with status %d\n", processes[i].pid, WEXITSTATUS(status));
        else
            fprintf(stderr, "[kernel] pid %d exited abnormally\n", processes[i].pid);
    }

    for (int i = 0; i < nprocesses; i++)
        close(processes[i].read_fd);
}

int main(void)
{
    for (uint32_t i = 1; i <= NUM_WORKERS; i++)
        fork_worker(i);
    fork_param_server();
    fork_monitor();

    register_all();
    run_rounds();
    shutdown_all();

      fprintf(stderr, "[kernel] all processes terminated, exiting\n");
      return 0;
}
