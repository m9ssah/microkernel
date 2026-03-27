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
 * what each worker does for your reference:
 *   1. registers with the kernel
 *   2. requests its data shard
 *   3. then each round receives weights, computes gradient, sends gradient back
 *   4. fianlly exits on OP_TERMINATE
 *
 * pipes (passed as file descriptors via argv):
 *   read_fd
 *   write_fd
 *   worker_id
 */

/* gradient computation (did logistic regression), more info at the bottom:)))))
 *
 * data format: each sample is (features..., label)
 * features: MAX_WEIGHT_SIZE - 1 floats
 * label:    1 float (0.0 or 1.0)
 *
 * logistic regression gradient (got this from internet, not sure if its right but it seems to work):
 *   for each sample i, its as follows:
 *     z       = dot(w, x_i)
 *     pred    = sigmoid(z)
 *     error   = pred - y_i
 *     grad   += error * x_i
 *     grad /= n_samples
 */

static float sigmoid(float z) {
    return 1.0f / (1.0f + expf(-z));
}

static void compute_gradient(
    const float *data,
    uint32_t     n_samples,
    uint32_t     n_features,
    const float *weights,
    float       *gradient
) {
    memset(gradient, 0, sizeof(float) * n_features);

    for (uint32_t i = 0; i < n_samples; i++) {
        const float *x = data + i * (n_features + 1);
        float        y = x[n_features];

        /* dot product (how fun)*/
        float z = 0.0f;
        for (uint32_t j = 0; j < n_features; j++) {
            z += weights[j] * x[j];
        }

        float error = sigmoid(z) - y;

        for (uint32_t j = 0; j < n_features; j++) {
            gradient[j] += error * x[j];
        }
    }

    /* average over samples */
    for (uint32_t j = 0; j < n_features; j++) {
        gradient[j] /= (float)n_samples;
    }
}

/* shard loading
 *
 * shard files are plain text, one sample per line:
 *
 * also written by gen_shards.py
 */
static float *load_shard(const char *path, uint32_t *out_samples, uint32_t *out_features) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "[worker] ERROR: cannot open shard file %s\n", path);
        return NULL;
    }

    /* first pass: count lines and features */
    uint32_t n_samples  = 0;
    uint32_t n_cols     = 0;
    char line[4096];

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '\n' || line[0] == '#') continue;
        if (n_cols == 0) {
            /* count space separated tokens on first data line */
            char tmp[4096];
            strncpy(tmp, line, sizeof(tmp));
            char *tok = strtok(tmp, " \t\n");
            while (tok) { n_cols++; tok = strtok(NULL, " \t\n"); }
        }
        n_samples++;
    }
    rewind(f);

    if (n_samples == 0 || n_cols < 2) {
        fclose(f);
        return NULL;
    }

    uint32_t n_features = n_cols - 1; /* last column is label */
    float *data = malloc(sizeof(float) * n_samples * n_cols);
    if (!data) { fclose(f); return NULL; }

    uint32_t row = 0;
    while (fgets(line, sizeof(line), f) && row < n_samples) {
        if (line[0] == '\n' || line[0] == '#') continue;
        char *tok = strtok(line, " \t\n");
        for (uint32_t c = 0; c < n_cols && tok; c++) {
            data[row * n_cols + c] = strtof(tok, NULL);
            tok = strtok(NULL, " \t\n");
        }
        row++;
    }
    fclose(f);

    *out_samples  = n_samples;
    *out_features = n_features;
    return data;
}

/* main func */

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <read_fd> <write_fd> <worker_id>\n", argv[0]);
        return 1;
    }

    int      read_fd  = atoi(argv[1]);
    int      write_fd = atoi(argv[2]);
    uint32_t worker_id = (uint32_t)atoi(argv[3]);

    fprintf(stderr, "[worker %u] started (read_fd=%d write_fd=%d)\n",
            worker_id, read_fd, write_fd);

    /* step 1: register with kernel  */
    RegisterPayload reg = { .servicetype = SERVICE_WORKER };
    if (send_message(write_fd, worker_id, SERVICE_KERNEL,
                     OP_REGISTER, &reg, sizeof(reg)) < 0) {
        fprintf(stderr, "[worker %u] ERROR: failed to send OP_REGISTER\n", worker_id);
        return 1;
    }

    /* wait for ACK */
    Message msg;
    if (recv_message(read_fd, &msg) < 0 || msg.header.opcode != OP_REGISTER_ACK) {
        fprintf(stderr, "[worker %u] ERROR: did not receive OP_REGISTER_ACK\n", worker_id);
        return 1;
    }
    fprintf(stderr, "[worker %u] registered with kernel\n", worker_id);

    /* step 2: load local shard from file
     * kernel assigns shard by worker_id; file is shard_<worker_id>.txt */
    char shard_path[64];
    snprintf(shard_path, sizeof(shard_path), "shard_%u.txt", worker_id);

    uint32_t n_samples = 0, n_features = 0;
    float *shard_data = load_shard(shard_path, &n_samples, &n_features);
    if (!shard_data) {
        fprintf(stderr, "[worker %u] ERROR: failed to load shard from %s\n",
                worker_id, shard_path);
        return 1;
    }
    fprintf(stderr, "[worker %u] loaded shard: %u samples, %u features\n",
            worker_id, n_samples, n_features);


    if (n_features > MAX_WEIGHT_SIZE) n_features = MAX_WEIGHT_SIZE;

    float gradient[MAX_WEIGHT_SIZE];
    float weights[MAX_WEIGHT_SIZE];

    /* main training loop (again double check later)*/
    while (1) {
        if (recv_message(read_fd, &msg) < 0) {
            fprintf(stderr, "[worker %u] pipe closed, exiting\n", worker_id);
            break;
        }

        switch (msg.header.opcode) {

        case OP_TERMINATE:
            fprintf(stderr, "[worker %u] received OP_TERMINATE, shutting down\n", worker_id);
            free(shard_data);
            close(read_fd);
            close(write_fd);
            return 0;

        case OP_HEARTBEAT_PING:
            /* respond immediately */
            send_message(write_fd, worker_id, msg.header.src_id,
                         OP_HEARTBEAT_PONG, NULL, 0);
            break;

        case OP_WEIGHTS: {
            /* unpack: WeightsPayload header followed by float array in payload */
            WeightsPayload *wp = (WeightsPayload *)msg.payload;
            uint32_t wcount = wp->weight_count;
            if (wcount > n_features) wcount = n_features;

            /* weights float array sits right after the WeightsPayload struct */
            float *w_data = (float *)(msg.payload + sizeof(WeightsPayload));
            memcpy(weights, w_data, sizeof(float) * wcount);

            fprintf(stderr, "[worker %u] received weights for round %u (%u weights)\n",
                    worker_id, wp->round_id, wcount);

            /* compute gradient on local shard */
            compute_gradient(shard_data, n_samples, n_features, weights, gradient);

            /* send gradient back: GradientPayload header + float array */
            uint8_t grad_buf[MAX_PAYLOAD_SIZE];
            GradientPayload *gp = (GradientPayload *)grad_buf;
            gp->round_id   = wp->round_id;
            gp->grad_count = n_features;
            memcpy(grad_buf + sizeof(GradientPayload), gradient,
                   sizeof(float) * n_features);

            uint32_t grad_total = sizeof(GradientPayload) + sizeof(float) * n_features;

            if (send_message(write_fd, worker_id, SERVICE_MODEL,
                             OP_SUBMIT_GRADIENT, grad_buf, grad_total) < 0) {
                fprintf(stderr, "[worker %u] ERROR: failed to send gradient\n", worker_id);
            } else {
                fprintf(stderr, "[worker %u] gradient submitted for round %u\n",
                        worker_id, wp->round_id);
            }
            break;
        }

        case OP_PAUSE:
            fprintf(stderr, "[worker %u] paused, waiting for OP_RESUME\n", worker_id);
            /* block until resume */
            while (recv_message(read_fd, &msg) == 0) {
                if (msg.header.opcode == OP_RESUME) {
                    fprintf(stderr, "[worker %u] resumed\n", worker_id);
                    break;
                }
                if (msg.header.opcode == OP_TERMINATE) {
                    free(shard_data);
                    return 0;
                }
            }
            break;

        default:
            fprintf(stderr, "[worker %u] WARNING: unknown opcode %u, ignoring\n",
                    worker_id, msg.header.opcode);
            break;
        }
    }

    free(shard_data);
    return 0;
}
