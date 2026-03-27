#ifndef NET_H
#define NET_H

#include <stddef.h>
#include <stdint.h>

#include "protocol.h"

int send_message(int fd, uint32_t src, uint32_t dest,
				 uint32_t opcode, const void *payload, uint32_t payload_size);

int recv_message(int fd, Message *msg);

#endif