#include "sysinit/sysinit.h"
#include "os/os.h"
#include "console/console.h"
#include "log/log.h"
#include "uart/uart.h"
#include <string.h>

/* ---- UART TLV receiver ---- */
#define TLV_MAX_VALUE_LEN   16

/* Command types (watch → BLE board) */
#define CMD_SEND_KEY        0x01  /* value: [keycode, modifier]  */
#define CMD_SEND_STRING     0x02  /* value: ASCII bytes          */
#define CMD_CLEAR_BONDS     0x03  /* value: (none)               */
#define CMD_BLE_CTRL        0x04  /* value: [0x00=off, 0x01=on]  */
#define CMD_PING            0x05  /* value: (none) — echoes ACK  */
#define CMD_GET_TIME        0x06  /* value: (none) — reads CTS   */

struct tlv_frame {
    uint8_t type;
    uint8_t length;
    uint8_t value[TLV_MAX_VALUE_LEN];
};

enum tlv_parse_state { TLV_TYPE, TLV_LENGTH, TLV_VALUE };

#define LOG_MODULE_MAIN (128)

/* Task */
#define MY_TASK_PRI         (1)
#define MY_STACK_SIZE       (512)
struct os_task my_task;
os_stack_t my_task_stack[MY_STACK_SIZE];

static struct uart_dev      *uart_dev;
static struct tlv_frame      tlv_rx;          /* filled by ISR  */
static struct tlv_frame      tlv_pending;     /* ready for task */
static uint8_t               tlv_idx;
static enum tlv_parse_state  tlv_state;
static volatile bool         tlv_frame_ready;
static struct os_event       uart_rx_event;

/* TX: small buffer for outgoing response frames */
static uint8_t               uart_tx_buf[8];
static uint8_t               uart_tx_len;
static volatile uint8_t      uart_tx_idx;

/* ISR: parse one incoming byte into the TLV state machine */
static int
uart_rx_char_cb(void *arg, uint8_t byte)
{
    switch (tlv_state) {
    case TLV_TYPE:
        tlv_rx.type = byte;
        tlv_state = TLV_LENGTH;
        return 0;

    case TLV_LENGTH:
        if (byte > TLV_MAX_VALUE_LEN) {
            tlv_state = TLV_TYPE;   /* invalid length — reset */
            return 0;
        }
        tlv_rx.length = byte;
        tlv_idx = 0;
        if (byte > 0) {
            tlv_state = TLV_VALUE;
            return 0;
        }
        /* zero-length frame — fall through to dispatch */
        break;

    case TLV_VALUE:
        tlv_rx.value[tlv_idx++] = byte;
        if (tlv_idx < tlv_rx.length) {
            return 0;
        }
        break;
    }

    /* Frame complete */
    tlv_pending = tlv_rx;
    tlv_frame_ready = true;
    tlv_state = TLV_TYPE;
    if (!uart_rx_event.ev_queued) {
        os_eventq_put(os_eventq_dflt_get(), &uart_rx_event);
    }
    return 0;
}

static int
uart_tx_char_cb(void *arg)
{
    if (uart_tx_idx >= uart_tx_len) {
        return -1;
    }
    return uart_tx_buf[uart_tx_idx++];
}

static void
uart_tx_send(const uint8_t *data, uint8_t len)
{
    if (len > sizeof(uart_tx_buf)) {
        return;
    }
    memcpy(uart_tx_buf, data, len);
    uart_tx_len = len;
    uart_tx_idx = 0;
    uart_start_tx(uart_dev);
}

/* Task context: dispatch the completed TLV frame */
static void
uart_dispatch(const struct tlv_frame *f)
{
    switch (f->type) {
    case CMD_SEND_KEY:
        MODLOG_INFO(LOG_MODULE_MAIN,
            "CMD_SEND_KEY: keycode=0x%02x modifier=0x%02x\n",
            f->length >= 1 ? f->value[0] : 0,
            f->length >= 2 ? f->value[1] : 0);
        break;

    case CMD_SEND_STRING:
        MODLOG_INFO(LOG_MODULE_MAIN,
            "CMD_SEND_STRING: len=%u\n", f->length);
        break;

    case CMD_CLEAR_BONDS:
        MODLOG_INFO(LOG_MODULE_MAIN, "CMD_CLEAR_BONDS\n");
        break;

    case CMD_BLE_CTRL:
        MODLOG_INFO(LOG_MODULE_MAIN,
            "CMD_BLE_CTRL: %s\n",
            (f->length >= 1 && f->value[0] == 0x00) ? "off" : "on");
        break;

    case CMD_PING: {
        static const uint8_t ack[] = { CMD_PING, 0x01, 0xAC };
        uart_tx_send(ack, sizeof(ack));
        MODLOG_INFO(LOG_MODULE_MAIN, "CMD_PING -> ACK sent\n");
        break;
    }

    case CMD_GET_TIME:
        MODLOG_INFO(LOG_MODULE_MAIN, "CMD_GET_TIME\n");
        break;

    default:
        MODLOG_INFO(LOG_MODULE_MAIN,
            "UART: unknown cmd 0x%02x len=%u\n", f->type, f->length);
        break;
    }
}

static void
uart_rx_event_cb(struct os_event *ev)
{
    MODLOG_INFO(LOG_MODULE_MAIN, "uart_rx_event_cb: event received\n");
    if (!tlv_frame_ready) {
        return;
    }
    tlv_frame_ready = false;
    uart_dispatch(&tlv_pending);
}

static void
uart_init(void)
{
    struct uart_conf uc = {
        .uc_speed    = 115200,
        .uc_databits = 8,
        .uc_stopbits = 1,
        .uc_parity   = UART_PARITY_NONE,
        .uc_flow_ctl = UART_FLOW_CTL_NONE,
        .uc_tx_char  = uart_tx_char_cb,
        .uc_rx_char  = uart_rx_char_cb,
    };

    uart_rx_event.ev_cb = uart_rx_event_cb;
    tlv_state = TLV_TYPE;

    uart_dev = (struct uart_dev *)os_dev_open("uart0", OS_TIMEOUT_NEVER, &uc);
    assert(uart_dev != NULL);

    MODLOG_INFO(LOG_MODULE_MAIN, "UART TLV receiver ready (115200 8N1)\n");
}

void my_task_func(void *arg) {
    while (1) {
        os_time_delay(os_time_ms_to_ticks32(1000));
    }
}

int
main(int argc, char **argv)
{
    sysinit();

    MODLOG_INFO(LOG_MODULE_MAIN, "TLV protocol test\n");

    uart_init();

    int rc = os_task_init(&my_task, "my_task", my_task_func, NULL, MY_TASK_PRI,
                 OS_WAIT_FOREVER, my_task_stack, MY_STACK_SIZE);
    assert(rc == 0);

    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }

    return 0;
}
