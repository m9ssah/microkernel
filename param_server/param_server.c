#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <math.h>
#include "../include/protocol.h"
#include "../include/payloads.h"
#include "../include/net.h"

/*
 * parameter server (SERVICE_MODEL) 
 *
 * what it does: 
 *   1. registers with kernel
 *   2. broadcasts weights to all workers
 *   3. collects gradients from workers
 *   4. averages gradients and updates weights (SGD)
 *   5. checks convergence via gradient norm
 *   6. terminates workers when done
 *
 * argv: param_server <kernel_read_fd> <kernel_write_fd> <num_workers>
 *                     <w1_read> <w1_write> [<w2_read> <w2_write> ...]
 */

#define MAX_WORKERS 16

typedef struct {
    int read_fd, write_fd, active;
} WorkerConn;

typedef struct {
    int          kernel_read_fd, kernel_write_fd;
    uint32_t     my_id;
    WorkerConn   workers[MAX_WORKERS];
    uint32_t     num_workers;
    float        weights[MAX_WEIGHT_SIZE];
    uint32_t     weight_count;
    float        learning_rate;
    uint32_t     max_rounds;
    float        convergence_threshold;
    uint32_t     current_round;
    float        current_loss;
} ParamServer;

/* PARAM SERVER FUNCTIONS */

/* sends OP_REGISTER as SERVICE_MODEL, receives OP_REGISTER_ACK */
static int register_with_kernel(ParamServer *ps) {
    RegisterPayload reg = { .servicetype = SERVICE_MODEL };
    if (send_message(ps->kernel_write_fd, 0, SERVICE_KERNEL,
                     OP_REGISTER, &reg, sizeof(reg)) < 0) {
        fprintf(stderr, "[param_server] ERROR: failed to send OP_REGISTER\n");
        return -1;
    }

    Message msg;
    if (recv_message(ps->kernel_read_fd, &msg) < 0 ||
        msg.header.opcode != OP_REGISTER_ACK) {
        fprintf(stderr, "[param_server] ERROR: did not receive OP_REGISTER_ACK\n");
        return -1;
    }

    RegisterAckPayload *ack = (RegisterAckPayload *)msg.payload;
    ps->my_id = ack->assigned_id;
    fprintf(stderr, "[param_server] registered with kernel (id=%u)\n", ps->my_id);
    return 0;
}

/* zero-initialize MAX_WEIGHT_SIZE(64) weights */
static void init_weights(ParamServer *ps) {
    ps->weight_count = MAX_WEIGHT_SIZE;
    memset(ps->weights, 0, sizeof(float) * ps->weight_count);
    fprintf(stderr, "[param_server] initialized %u weights to 0.0\n", ps->weight_count);
}

/* packs WeightsPayload + float array and sends OP_WEIGHTS to each active worker */
static void broadcast_weights(ParamServer *ps) {
    /* pack WeightsPayload header + float array, matching worker.c unpack (line 261-267) */
    uint8_t buf[MAX_PAYLOAD_SIZE];
    WeightsPayload *wp = (WeightsPayload *)buf;
    wp->round_id     = ps->current_round;
    wp->weight_count = ps->weight_count;

    uint32_t floats_size = sizeof(float) * ps->weight_count;
    uint32_t total_size  = sizeof(WeightsPayload) + floats_size;

    if (total_size > MAX_PAYLOAD_SIZE) {
        fprintf(stderr, "[param_server] ERROR: weights payload too large (%u > %u)\n",
                total_size, MAX_PAYLOAD_SIZE);
        return;
    }

    memcpy(buf + sizeof(WeightsPayload), ps->weights, floats_size);

    for (uint32_t i = 0; i < ps->num_workers; i++) {
        if (!ps->workers[i].active) continue;

        if (send_message(ps->workers[i].write_fd, ps->my_id, i,
                         OP_WEIGHTS, buf, total_size) < 0) {
            fprintf(stderr, "[param_server] ERROR: failed to send weights to worker %u\n", i);
            ps->workers[i].active = 0;
        }
    }
}

/* blocks recv from each active worker expecting OP_SUBMIT_GRADIENT, validates round_id, accumulates and averages gradients */
static int collect_gradients(ParamServer *ps, float *avg_gradient) {
    memset(avg_gradient, 0, sizeof(float) * ps->weight_count);
    uint32_t responses = 0;

    for (uint32_t i = 0; i < ps->num_workers; i++) {
        if (!ps->workers[i].active) continue;

        Message msg;
        if (recv_message(ps->workers[i].read_fd, &msg) < 0) {
            fprintf(stderr, "[param_server] ERROR: failed to recv gradient from worker %u\n", i);
            ps->workers[i].active = 0;
            continue;
        }

        if (msg.header.opcode != OP_SUBMIT_GRADIENT) {
            fprintf(stderr, "[param_server] WARNING: expected OP_SUBMIT_GRADIENT from worker %u, got %u\n",
                    i, msg.header.opcode);
            continue;
        }

        GradientPayload *gp = (GradientPayload *)msg.payload;

        if (gp->round_id != ps->current_round) {
            fprintf(stderr, "[param_server] WARNING: worker %u sent gradient for round %u, expected %u\n",
                    i, gp->round_id, ps->current_round);
            continue;
        }

        uint32_t gcount = gp->grad_count;
        if (gcount > ps->weight_count) gcount = ps->weight_count;

        float *g_data = (float *)(msg.payload + sizeof(GradientPayload));
        for (uint32_t j = 0; j < gcount; j++) {
            avg_gradient[j] += g_data[j];
        }
        responses++;
    }

    if (responses == 0) return -1;

    /* average */
    for (uint32_t j = 0; j < ps->weight_count; j++) {
        avg_gradient[j] /= (float)responses;
    }

    fprintf(stderr, "[param_server] round %u: collected gradients from %u workers\n",
            ps->current_round, responses);
    return (int)responses;
}

/* SGD step: w -= lr * grad */
static void update_weights(ParamServer *ps, const float *avg_gradient) {
    for (uint32_t j = 0; j < ps->weight_count; j++) {
        ps->weights[j] -= ps->learning_rate * avg_gradient[j];
    }
}

/* calculating root mean square (RMS) of averaged gradient - we will use this as a proxy for convergence */
static float compute_gradient_norm(const float *gradient, uint32_t count) {
    float sum = 0.0f;
    for (uint32_t j = 0; j < count; j++) {
        sum += gradient[j] * gradient[j];
    }
    return sqrtf(sum / (float)count);
}

/* this will run until it reaches 100 rounds OR when convergence is achieved OR no more active workers */
static void training_loop(ParamServer *ps) {
    float avg_gradient[MAX_WEIGHT_SIZE];

    for (ps->current_round = 1; ps->current_round <= ps->max_rounds; ps->current_round++) {
        /* count active workers */
        uint32_t active = 0;
        for (uint32_t i = 0; i < ps->num_workers; i++) {
            if (ps->workers[i].active) active++;
        }
        if (active == 0) {
            fprintf(stderr, "[param_server] no active workers, stopping training\n");
            break;
        }

        broadcast_weights(ps);

        int resp = collect_gradients(ps, avg_gradient);
        if (resp < 0) {
            fprintf(stderr, "[param_server] no gradients received, stopping training\n");
            break;
        }

        update_weights(ps, avg_gradient);

        float grad_norm = compute_gradient_norm(avg_gradient, ps->weight_count);
        ps->current_loss = grad_norm;

        fprintf(stderr, "[param_server] round %u: grad_norm=%.6f lr=%.4f active_workers=%u\n",
                ps->current_round, grad_norm, ps->learning_rate, active);

        if (grad_norm < ps->convergence_threshold) {
            fprintf(stderr, "[param_server] converged at round %u (grad_norm=%.6f < %.6f)\n",
                    ps->current_round, grad_norm, ps->convergence_threshold);
            break;
        }
    }

    fprintf(stderr, "[param_server] training complete after %u rounds\n", ps->current_round);
}

/* sends OP_TERMINATE to all active workers */
static void terminate_workers(ParamServer *ps) {
    for (uint32_t i = 0; i < ps->num_workers; i++) {
        if (!ps->workers[i].active) continue;

        if (send_message(ps->workers[i].write_fd, ps->my_id, i,
                         OP_TERMINATE, NULL, 0) < 0) {
            fprintf(stderr, "[param_server] WARNING: failed to send OP_TERMINATE to worker %u\n", i);
        }
    }
    fprintf(stderr, "[param_server] sent OP_TERMINATE to all active workers\n");
}

/* runs the full lifecycle yay */
int main(int argc, char *argv[]) {
    /* argv: param_server <kernel_read_fd> <kernel_write_fd> <num_workers>
     *                     <w1_read> <w1_write> [<w2_read> <w2_write> ...] */
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <kernel_read_fd> <kernel_write_fd> <num_workers> "
                        "<w1_read> <w1_write> ...\n", argv[0]);
        return 1;
    }

    ParamServer ps;
    memset(&ps, 0, sizeof(ps));

    ps.kernel_read_fd       = atoi(argv[1]);
    ps.kernel_write_fd      = atoi(argv[2]);
    ps.num_workers          = (uint32_t)atoi(argv[3]);
    ps.learning_rate        = 0.01f;
    ps.max_rounds           = 100;
    ps.convergence_threshold = 1e-4f;

    if (ps.num_workers > MAX_WORKERS) {
        fprintf(stderr, "[param_server] ERROR: num_workers %u exceeds MAX_WORKERS %d\n",
                ps.num_workers, MAX_WORKERS);
        return 1;
    }

    int expected_argc = 4 + (int)(ps.num_workers * 2);
    if (argc < expected_argc) {
        fprintf(stderr, "[param_server] ERROR: expected %d args, got %d\n",
                expected_argc, argc);
        return 1;
    }

    for (uint32_t i = 0; i < ps.num_workers; i++) {
        ps.workers[i].read_fd  = atoi(argv[4 + i * 2]);
        ps.workers[i].write_fd = atoi(argv[5 + i * 2]);
        ps.workers[i].active   = 1;
    }

    fprintf(stderr, "[param_server] started (kernel_fds=%d/%d, %u workers)\n",
            ps.kernel_read_fd, ps.kernel_write_fd, ps.num_workers);

    if (register_with_kernel(&ps) < 0) return 1;

    init_weights(&ps);
    training_loop(&ps);
    terminate_workers(&ps);

    /* cleanup */
    close(ps.kernel_read_fd);
    close(ps.kernel_write_fd);
    for (uint32_t i = 0; i < ps.num_workers; i++) {
        close(ps.workers[i].read_fd);
        close(ps.workers[i].write_fd);
    }

    fprintf(stderr, "[param_server] shutdown complete\n");
    return 0;
}
