#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "../include/protocol.h"
#include "../include/payloads.h"
#include "../include/net.h"

#define FIFO_PATH "/tmp/monitor_fifo"

static int read_menu_choice(FILE *cmd)
{
    char line[64];

    if (fgets(line, sizeof(line), cmd) == NULL)
        return -1;

    return atoi(line);
}

static float read_float_value(FILE *cmd)
{
    char line[64];

    if (fgets(line, sizeof(line), cmd) == NULL)
        return 0.0f;

    return strtof(line, NULL);
}

int main(void)
{
    uint32_t monitor_id;
    Message msg;

    unlink(FIFO_PATH);
    if (mkfifo(FIFO_PATH, 0666) < 0)
    {
        perror("mkfifo");
        return 1;
    }

    RegisterPayload reg;
    reg.servicetype = SERVICE_MONITOR;

    if (send_message(STDOUT_FILENO, SERVICE_MONITOR, SERVICE_KERNEL,
                     OP_REGISTER, &reg, sizeof(reg)) < 0)
    {
        fprintf(stderr, "[monitor] ERROR: failed to send OP_REGISTER\n");
        unlink(FIFO_PATH);
        return 1;
    }

    if (recv_message(STDIN_FILENO, &msg) < 0 || msg.header.opcode != OP_REGISTER_ACK)
    {
        fprintf(stderr, "[monitor] ERROR: did not receive OP_REGISTER_ACK\n");
        unlink(FIFO_PATH);
        return 1;
    }

    monitor_id = ((RegisterAckPayload *)msg.payload)->assigned_id;
    fprintf(stderr, "[monitor] registered with id=%u\n", monitor_id);

    fprintf(stderr, "[monitor] command FIFO created at %s\n", FIFO_PATH);
    fprintf(stderr, "[monitor] send commands from another terminal:\n");
    fprintf(stderr, "          echo '1' > %s          # Query Progress\n", FIFO_PATH);
    fprintf(stderr, "          echo '2' > %s          # Adjust LR (make sure to follow with LR value)\n", FIFO_PATH);
    fprintf(stderr, "          echo '3' > %s          # Pause\n", FIFO_PATH);
    fprintf(stderr, "          echo '4' > %s          # Resume\n", FIFO_PATH);
    fprintf(stderr, "          echo '5' > %s          # Quit\n", FIFO_PATH);

    int fifo_fd = open(FIFO_PATH, O_RDWR);
    if (fifo_fd < 0)
    {
        perror("open");
        unlink(FIFO_PATH);
        return 1;
    }

    FILE *cmd = fdopen(fifo_fd, "r");
    if (!cmd)
    {
        perror("fdopen");
        close(fifo_fd);
        unlink(FIFO_PATH);
        return 1;
    }

    while (1)
    {
        int choice = read_menu_choice(cmd);
        if (choice < 0)
        {
            if (feof(cmd))
            {
                clearerr(cmd);
                continue;
            }
            break;
        }

        if (choice == 1)
        {
            if (send_message(STDOUT_FILENO, monitor_id, SERVICE_KERNEL,
                             OP_QUERY_PROGRESS, NULL, 0) < 0)
            {
                fprintf(stderr, "[monitor] ERROR: failed to send OP_QUERY_PROGRESS\n");
                continue;
            }

            if (recv_message(STDIN_FILENO, &msg) < 0 || msg.header.opcode != OP_PROGRESS)
            {
                fprintf(stderr, "[monitor] ERROR: failed to receive OP_PROGRESS\n");
                continue;
            }

            ProgressRespPayload *progress = (ProgressRespPayload *)msg.payload;
            fprintf(stderr, "[monitor] progress: total_rounds=%u current_round=%u active=%u failed=%u lr=%.6f loss=%.6f\n",
                    progress->total_rounds,
                    progress->current_round,
                    progress->active_workers,
                    progress->failed_workers,
                    progress->current_lr,
                    progress->current_loss);
        }
        else if (choice == 2)
        {
            AdjustLRPayload adjust;

            fprintf(stderr, "Enter new learning rate: ");
            adjust.new_lr = read_float_value(cmd);
            adjust.round_id = 0;

            if (send_message(STDOUT_FILENO, monitor_id, SERVICE_KERNEL,
                             OP_ADJUST_LR, &adjust, sizeof(adjust)) < 0)
            {
                fprintf(stderr, "[monitor] ERROR: failed to send OP_ADJUST_LR\n");
            }
            else
            {
                fprintf(stderr, "[monitor] requested learning rate update to %.6f\n", adjust.new_lr);
            }
        }
        else if (choice == 3)
        {
            if (send_message(STDOUT_FILENO, monitor_id, SERVICE_KERNEL,
                             OP_PAUSE, NULL, 0) < 0)
            {
                fprintf(stderr, "[monitor] ERROR: failed to send OP_PAUSE\n");
            }
            else
            {
                fprintf(stderr, "[monitor] pause requested\n");
            }
        }
        else if (choice == 4)
        {
            if (send_message(STDOUT_FILENO, monitor_id, SERVICE_KERNEL,
                             OP_RESUME, NULL, 0) < 0)
            {
                fprintf(stderr, "[monitor] ERROR: failed to send OP_RESUME\n");
            }
            else
            {
                fprintf(stderr, "[monitor] resume requested\n");
            }
        }
        else if (choice == 5)
        {
            send_message(STDOUT_FILENO, monitor_id, SERVICE_KERNEL,
                         OP_TERMINATE, NULL, 0);
            fprintf(stderr, "[monitor] quitting\n");
            break;
        }
        else
        {
            fprintf(stderr, "[monitor] invalid option\n");
        }
    }
    fclose(cmd);
    unlink(FIFO_PATH);
    return 0;
}
