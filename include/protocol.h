#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define PROTOCOL_MAGIC 0xDEADC0DE
#define MAX_PAYLOAD_SIZE 512 // TODO: adjust as needed

typedef enum
{
    SERVICE_KERNEL = 0,
    SERVICE_DATA,
    SERVICE_MODEL,
    SERVICE_SCHEDULER,
    SERVICE_MONITOR,
    SERVICE_WORKER
} ServiceType;

typedef enum
{
    OP_REGISTER,
    OP_REGISTER_ACK,
    OP_TERMINATE,

    OP_GET_WEIGHTS,
    OP_WEIGHTS,
    OP_SUBMIT_GRADIENT,

    OP_ROUND_START,
    OP_ROUND_COMPLETE,
    OP_HEARTBEAT_PING,
    OP_HEARTBEAT_PONG,

    OP_QUERY_PROGRESS,
    OP_PROGRESS,
    OP_PAUSE,
    OP_RESUME,
    OP_ADJUST_LR,
} Opcode;

typedef struct
{
    uint32_t magic; // for protocol validation
    uint32_t src_id;
    uint32_t dest_id;
    uint32_t opcode;
    uint32_t msg_id;
    uint32_t payload_size;
} MessageHeader;

typedef struct
{
    MessageHeader header;
    uint8_t payload[MAX_PAYLOAD_SIZE];
} Message;

#endif