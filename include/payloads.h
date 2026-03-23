#ifndef PAYLOADS_H
#define PAYLOADS_H

#include <stdint.h>

typedef struct {
    uint32_t servicetype;
} RegisterPayload;

typedef struct {
    uint32_t servicetype;
    uint32_t assigned_id;
} RegisterAckPayload;

typedef struct {
    uint32_t round_id;
} CheckActivePayload;

typedef struct {
    uint32_t is_active;
    uint32_t round_id;
    uint32_t shard_id;
} ActiveRespPayload;

typedef struct {
    uint32_t round_id;
} RoundStartPayload;

typedef struct {
    uint32_t round_id;
    uint8_t  status;
} RoundComepletePayload;

typedef struct {
    uint32_t round_id;
    uint32_t shard_id;
} GetShardPayload;

typedef struct {
    uint32_t shard_id;
    uint32_t shard_size;
} ShardDataPayload;

typedef struct {
    uint32_t round_id;
} GetWeightsPayload;

typedef struct {
    uint32_t round_id;
    uint32_t weight_count;
} WeightsPayload;

typedef struct {
    uint32_t round_id;
    uint32_t grad_count;
} GradientPayload;

typedef struct {
    uint32_t total_rounds;
    uint32_t current_round;
    uint32_t active_workers;
    uint32_t failed_workers;
    float    current_lr;
    float    current_loss;
} ProgressRespPayload;

typedef struct {
    uint32_t round_id;
    float new_lr;
} AdjustLRPayload;

#endif