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
 * message-driven service that communicates only through the kernel via stdin/stdout.
 *
 * what it does:
 *   1. registers with kernel
 *   2. handles messages in a loop:
 *      - OP_GET_WEIGHTS: process accumulated gradients (if any), send updated weights
 *      - OP_SUBMIT_GRADIENT: accumulate gradient from a worker
 *      - OP_QUERY_PROGRESS: respond with training stats
 *      - OP_ADJUST_LR: update learning rate
 *      - OP_TERMINATE: exit
 *
 * argv: param_server <num_workers>
 */

typedef struct {
    uint32_t     my_id;
    uint32_t     num_workers;
    float        weights[MAX_WEIGHT_SIZE];
    uint32_t     weight_count;
    float        learning_rate;
    float        convergence_threshold;
    uint32_t     current_round;
    float        current_loss;
    float        accumulated_gradients[MAX_WEIGHT_SIZE];
    uint32_t     gradient_responses;
} ParamServer;

/* PARAM SERVER FUNCTIONS */

static int register_with_kernel(ParamServer *ps) {
    RegisterPayload reg = { .servicetype = SERVICE_MODEL };
    if (send_message(STDOUT_FILENO, 0, SERVICE_KERNEL,
                     OP_REGISTER, &reg, sizeof(reg)) < 0) {
        fprintf(stderr, "[param_server] ERROR: failed to send OP_REGISTER\n");
        return -1;
    }

    Message msg;
    if (recv_message(STDIN_FILENO, &msg) < 0 ||
        msg.header.opcode != OP_REGISTER_ACK) {
        fprintf(stderr, "[param_server] ERROR: did not receive OP_REGISTER_ACK\n");
        return -1;
    }

    RegisterAckPayload *ack = (RegisterAckPayload *)msg.payload;
    ps->my_id = ack->assigned_id;
    fprintf(stderr, "[param_server] registered with kernel (id=%u)\n", ps->my_id);
    return 0;
}

static void init_weights(ParamServer *ps) {
    ps->weight_count = MAX_WEIGHT_SIZE;
    memset(ps->weights, 0, sizeof(float) * ps->weight_count);
    fprintf(stderr, "[param_server] initialized %u weights to 0.0\n", ps->weight_count);
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

static void send_weights(ParamServer *ps) {
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

    send_message(STDOUT_FILENO, ps->my_id, SERVICE_KERNEL,
                 OP_WEIGHTS, buf, total_size);
}

static void handle_get_weights(ParamServer *ps, Message *msg) {
    GetWeightsPayload *gw = (GetWeightsPayload *)msg->payload;
    ps->current_round = gw->round_id;

    if (ps->gradient_responses > 0) {
        /* average accumulated gradients */
        for (uint32_t j = 0; j < ps->weight_count; j++) {
            ps->accumulated_gradients[j] /= (float)ps->gradient_responses;
        }

        update_weights(ps, ps->accumulated_gradients);

        float grad_norm = compute_gradient_norm(ps->accumulated_gradients, ps->weight_count);
        ps->current_loss = grad_norm;

        fprintf(stderr, "[param_server] round %u: grad_norm=%.6f lr=%.4f responses=%u\n",
                ps->current_round, grad_norm, ps->learning_rate, ps->gradient_responses);

        /* reset for next round */
        memset(ps->accumulated_gradients, 0, sizeof(float) * ps->weight_count);
        ps->gradient_responses = 0;

        if (grad_norm < ps->convergence_threshold) {
            fprintf(stderr, "[param_server] converged at round %u (grad_norm=%.6f < %.6f)\n",
                    ps->current_round, grad_norm, ps->convergence_threshold);

            RoundCompletePayload rc;
            rc.round_id = ps->current_round;
            rc.status = 1;
            send_message(STDOUT_FILENO, ps->my_id, SERVICE_KERNEL,
                         OP_ROUND_COMPLETE, &rc, sizeof(rc));
            return;
        }
    }

    send_weights(ps);
}

static void handle_submit_gradient(ParamServer *ps, Message *msg) {
    GradientPayload *gp = (GradientPayload *)msg->payload;
    uint32_t gcount = gp->grad_count;
    if (gcount > ps->weight_count) gcount = ps->weight_count;

    float *g_data = (float *)(msg->payload + sizeof(GradientPayload));
    for (uint32_t j = 0; j < gcount; j++) {
        ps->accumulated_gradients[j] += g_data[j];
    }
    ps->gradient_responses++;

    fprintf(stderr, "[param_server] received gradient for round %u (total responses: %u)\n",
            gp->round_id, ps->gradient_responses);
}

static void handle_query_progress(ParamServer *ps) {
    ProgressRespPayload progress;
    progress.total_rounds = 0;
    progress.current_round = ps->current_round;
    progress.active_workers = ps->num_workers;
    progress.failed_workers = 0;
    progress.current_lr = ps->learning_rate;
    progress.current_loss = ps->current_loss;

    send_message(STDOUT_FILENO, ps->my_id, SERVICE_KERNEL,
                 OP_PROGRESS, &progress, sizeof(progress));
}

static void handle_adjust_lr(ParamServer *ps, Message *msg) {
    AdjustLRPayload *adjust = (AdjustLRPayload *)msg->payload;
    ps->learning_rate = adjust->new_lr;
    fprintf(stderr, "[param_server] learning rate adjusted to %.6f\n", ps->learning_rate);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <num_workers>\n", argv[0]);
        return 1;
    }

    ParamServer ps;
    memset(&ps, 0, sizeof(ps));

    ps.num_workers           = (uint32_t)atoi(argv[1]);
    ps.learning_rate         = 0.01f;
    ps.convergence_threshold = 1e-4f;

    fprintf(stderr, "[param_server] started (%u workers)\n", ps.num_workers);

    if (register_with_kernel(&ps) < 0) return 1;

    init_weights(&ps);

    /* message-driven loop: respond to kernel requests */
    Message msg;
    while (1) {
        if (recv_message(STDIN_FILENO, &msg) < 0) {
            fprintf(stderr, "[param_server] pipe closed, exiting\n");
            break;
        }

        switch (msg.header.opcode) {
        case OP_GET_WEIGHTS:
            handle_get_weights(&ps, &msg);
            break;

        case OP_SUBMIT_GRADIENT:
            handle_submit_gradient(&ps, &msg);
            break;

        case OP_QUERY_PROGRESS:
            handle_query_progress(&ps);
            break;

        case OP_ADJUST_LR:
            handle_adjust_lr(&ps, &msg);
            break;

        case OP_TERMINATE:
            fprintf(stderr, "[param_server] received OP_TERMINATE, shutting down\n");
            return 0;

        default:
            fprintf(stderr, "[param_server] WARNING: unknown opcode %u, ignoring\n",
                    msg.header.opcode);
            break;
        }
    }

    fprintf(stderr, "[param_server] shutdown complete\n");
    return 0;
}
