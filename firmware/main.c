#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "uart.h"
#include "fastmath.h"
#include "shard_data.h"

#include "protocol.h"
#include "payloads.h"

_Static_assert(sizeof(MessageHeader) == 24, "MessageHeader must be 24 bytes");
_Static_assert(sizeof(Message) == 536, "Message must be 536 bytes on the wire");

#define FW_INITIAL_ID 4u

#define IDLE_TIMEOUT_MS 500u

#define PROTO_TASK_STACK_WORDS 512
#define PROTO_TASK_PRIORITY    2 

//state
static union
{
    Message msg;
    uint8_t bytes[sizeof(Message)];
} g_rx;

static union
{
    Message msg;
    uint8_t bytes[sizeof(Message)];
} g_tx;

static union
{
    GradientPayload gp;
    uint8_t bytes[sizeof(GradientPayload) + sizeof(float) * SHARD_N_FEATURES];
} g_grad;

static size_t   g_rx_len;        // bytes of g_rx currently filled
static uint32_t g_resyncs;       // times we discarded a byte hunting magic
static uint32_t g_next_msg_id = 1;

static uint32_t g_my_id = FW_INITIAL_ID;
static int      g_registered;
static int      g_paused;
static int      g_terminated;

static float g_weights[SHARD_N_FEATURES];
static int   g_has_weights;
static float g_gradient[SHARD_N_FEATURES];

static const uint8_t MAGIC_BYTES[4] = {0xDE, 0xC0, 0xAD, 0xDE};

// transmit
static void fw_send(uint32_t dest, uint32_t opcode,
                    const void *payload, uint32_t payload_size)
{
    if (payload_size > MAX_PAYLOAD_SIZE)
    {
        return;
    }

    memset(&g_tx.msg, 0, sizeof(g_tx.msg));
    g_tx.msg.header.magic = PROTOCOL_MAGIC;
    g_tx.msg.header.src_id = g_my_id;
    g_tx.msg.header.dest_id = dest;
    g_tx.msg.header.opcode = opcode;
    g_tx.msg.header.msg_id = g_next_msg_id++;
    g_tx.msg.header.payload_size = payload_size;

    if (payload != NULL && payload_size > 0)
    {
        memcpy(g_tx.msg.payload, payload, payload_size);
    }

    uart0_write(g_tx.bytes, sizeof(Message));
}

static void send_register(void)
{
    RegisterPayload reg;
    reg.servicetype = SERVICE_WORKER;
    fw_send(SERVICE_KERNEL, OP_REGISTER, &reg, sizeof(reg));
}

// compute gradient
static void compute_gradient(const float *weights, float *gradient)
{
    for (uint32_t j = 0; j < SHARD_N_FEATURES; j++)
    {
        gradient[j] = 0.0f;
    }

    for (uint32_t i = 0; i < SHARD_N_SAMPLES; i++)
    {
        /* Each row is SHARD_N_FEATURES features followed by the label. */
        const float *x = &g_shard_data[i * (SHARD_N_FEATURES + 1)];
        float y = x[SHARD_N_FEATURES];

        float z = 0.0f;
        for (uint32_t j = 0; j < SHARD_N_FEATURES; j++)
        {
            z += weights[j] * x[j];
        }

        float error = fm_sigmoidf(z) - y;

        for (uint32_t j = 0; j < SHARD_N_FEATURES; j++)
        {
            gradient[j] += error * x[j];
        }
    }

    for (uint32_t j = 0; j < SHARD_N_FEATURES; j++)
    {
        gradient[j] /= (float)SHARD_N_SAMPLES;
    }
}

static void send_gradient(uint32_t round_id)
{
    g_grad.gp.round_id = round_id;
    g_grad.gp.grad_count = SHARD_N_FEATURES;
    memcpy(g_grad.bytes + sizeof(GradientPayload), g_gradient,
           sizeof(float) * SHARD_N_FEATURES);

    fw_send(SERVICE_MODEL, OP_SUBMIT_GRADIENT, g_grad.bytes, sizeof(g_grad.bytes));
}

// message dispatch
static void handle_message(const Message *msg)
{
    switch (msg->header.opcode)
    {

    case OP_REGISTER_ACK:
    {
        const RegisterAckPayload *ack = (const RegisterAckPayload *)(const void *)msg->payload;
        g_my_id = ack->assigned_id;
        g_registered = 1;
        dbg_puts("[fw] registered, id=");
        dbg_putu(g_my_id);
        dbg_puts("\r\n");
        break;
    }

    case OP_HEARTBEAT_PING:
        fw_send(msg->header.src_id, OP_HEARTBEAT_PONG, NULL, 0);
        break;

    case OP_WEIGHTS:
    {
        const WeightsPayload *wp = (const WeightsPayload *)(const void *)msg->payload;
        uint32_t wcount = wp->weight_count;

        if (wcount > SHARD_N_FEATURES)
        {
            wcount = SHARD_N_FEATURES;
        }

        memcpy(g_weights, msg->payload + sizeof(WeightsPayload),
               sizeof(float) * wcount);
        g_has_weights = 1;
        break;
    }

    case OP_ROUND_START:
    {
        const RoundStartPayload *rs = (const RoundStartPayload *)(const void *)msg->payload;

        if (g_paused)
        {
            dbg_puts("[fw] round start while paused\r\n");
        }

        if (g_has_weights)
        {
            compute_gradient(g_weights, g_gradient);
        }

        send_gradient(rs->round_id);
        break;
    }

    case OP_PAUSE:
        g_paused = 1;
        dbg_puts("[fw] paused\r\n");
        break;

    case OP_RESUME:
        g_paused = 0;
        dbg_puts("[fw] resumed\r\n");
        break;

    case OP_TERMINATE:
        g_terminated = 1;
        dbg_puts("[fw] terminate\r\n");
        break;

    default:
        break;
    }
}

// frame accumulation
static void protocol_feed_byte(uint8_t b)
{
    if (g_rx_len < sizeof(MAGIC_BYTES))
    {
        if (b == MAGIC_BYTES[g_rx_len])
        {
            g_rx.bytes[g_rx_len++] = b;
        }
        else if (b == MAGIC_BYTES[0])
        {
            g_rx.bytes[0] = b;
            g_rx_len = 1;
            g_resyncs++;
        }
        else
        {
            g_rx_len = 0;
            g_resyncs++;
        }
        return;
    }

    g_rx.bytes[g_rx_len++] = b;

    if (g_rx_len == sizeof(Message))
    {
        handle_message(&g_rx.msg);
        g_rx_len = 0;
    }
}

static void vProtocolTask(void *pvParameters)
{
    QueueHandle_t q = uart0_rx_queue();

    (void)pvParameters;

    send_register();

    for (;;)
    {
        uint8_t byte;

        if (xQueueReceive(q, &byte, pdMS_TO_TICKS(IDLE_TIMEOUT_MS)) == pdTRUE)
        {
            protocol_feed_byte(byte);
        }
        else if (!g_registered)
        {
            dbg_puts("[fw] no ack yet, resending register\r\n");
            send_register();
        }
        else if (g_terminated)
        {
            vTaskDelay(pdMS_TO_TICKS(IDLE_TIMEOUT_MS));
        }
    }
}


void vAssertCalled(const char *pcFile, unsigned long ulLine)
{
    taskDISABLE_INTERRUPTS();
    dbg_puts("\r\n[fw] ASSERT FAILED: ");
    dbg_puts(pcFile);
    dbg_puts(":");
    dbg_putu((uint32_t)ulLine);
    dbg_puts("\r\n");
    for (;;)
    {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    taskDISABLE_INTERRUPTS();
    dbg_puts("\r\n[fw] STACK OVERFLOW in task: ");
    dbg_puts(pcTaskName);
    dbg_puts("\r\n");
    for (;;)
    {
    }
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    dbg_puts("\r\n[fw] MALLOC FAILED (configTOTAL_HEAP_SIZE too small)\r\n");
    for (;;)
    {
    }
}


int main(void)
{
    dbg_init();
    dbg_puts("\r\n[fw] LM3S6965 firmware worker booting\r\n");
    dbg_puts("[fw] shard ");
    dbg_putu(SHARD_ID);
    dbg_puts(": ");
    dbg_putu(SHARD_N_SAMPLES);
    dbg_puts(" samples, ");
    dbg_putu(SHARD_N_FEATURES);
    dbg_puts(" features\r\n");

    {
        uint32_t magic = PROTOCOL_MAGIC;
        configASSERT(memcmp(&magic, MAGIC_BYTES, sizeof(MAGIC_BYTES)) == 0);
    }

    uart0_init();

    if (xTaskCreate(vProtocolTask, "proto", PROTO_TASK_STACK_WORDS,
                    NULL, PROTO_TASK_PRIORITY, NULL) != pdPASS)
    {
        dbg_puts("[fw] FATAL: could not create protocol task\r\n");
        for (;;)
        {
        }
    }

    dbg_puts("[fw] starting scheduler\r\n");
    vTaskStartScheduler();

    dbg_puts("[fw] FATAL: scheduler returned\r\n");
    for (;;)
    {
    }
}
