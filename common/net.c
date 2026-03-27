#include "net.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static uint32_t next_msg_id = 1;

static int read_exact(int fd, void *buf, size_t n)
{
    size_t total = 0;
    while (total < n)
    {
        ssize_t r = read(fd, (char *)buf + total, n - total);
        if (r <= 0)
            return -1;
        total += r;
    }
    return 0;
}

static int write_exact(int fd, const void *buf, size_t n)
{
    size_t total = 0;
    while (total < n)
    {
        ssize_t w = write(fd, (const char *)buf + total, n - total);
        if (w <= 0)
            return -1;
        total += w;
    }
    return 0;
}

int send_message(int fd, uint32_t src, uint32_t dest,
                 uint32_t opcode, const void *payload, uint32_t payload_size)
{
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.magic = PROTOCOL_MAGIC;
    msg.header.src_id = src;
    msg.header.dest_id = dest;
    msg.header.opcode = opcode;
    msg.header.msg_id = next_msg_id++;
    msg.header.payload_size = payload_size;
    if (payload_size > MAX_PAYLOAD_SIZE)
        return -1;
    if (payload && payload_size > 0)
    {
        memcpy(msg.payload, payload, payload_size);
    }
    return write_exact(fd, &msg, sizeof(Message));
}

int recv_message(int fd, Message *msg)
{
    if (read_exact(fd, msg, sizeof(Message)) < 0)
        return -1;
    if (msg->header.magic != PROTOCOL_MAGIC)
    {
        fprintf(stderr, "[net] ERROR: bad magic 0x%08X\n", msg->header.magic);
        return -1;
    }
    return 0;
}
