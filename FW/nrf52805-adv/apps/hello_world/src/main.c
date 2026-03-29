#include "sysinit/sysinit.h"
#include "os/os.h"
#include "console/console.h"
#include "log/log.h"
#include "uart/uart.h"


/* Define task stack and task object */
#define MY_TASK_PRI         (1)
#define MY_STACK_SIZE       (512)
struct os_task my_task;
os_stack_t my_task_stack[MY_STACK_SIZE];

#define LOG_MODULE_MAIN (128)

static struct uart_dev *uart_dev;
struct os_event rx_ev;
/* Small ring buffer */
static uint8_t g_tx_buf[64];
static int g_tx_head, g_tx_tail;

static void
uart_console_rx_char_event(struct os_event *ev)
{
    /* This is where we would process received UART data. For this example, we just log that we received something. */
    if (g_tx_head == g_tx_tail) {
        return;  /* No data */
    }
    uint8_t b = g_tx_buf[g_tx_tail];
    g_tx_tail = (g_tx_tail + 1) % sizeof(g_tx_buf);
    MODLOG_INFO(LOG_MODULE_MAIN, "Received a byte over UART: 0x%02X\n", b);
}

static int
uart_tx_char_cb(void *arg)
{
    return -1; // No data to send
}

static int
uart_rx_char_cb(void *arg, uint8_t byte)
{
    int next = (g_tx_head + 1) % sizeof(g_tx_buf);
    // if (next == g_tx_tail) {
    //     return -1;  /* TX buffer full — stall RX */
    // }
    g_tx_buf[g_tx_head] = byte;
    g_tx_head = next;
    /* This callback is called with interrupts disabled, so we should not do any
     * heavy processing here. Instead, we can queue an event to process the
     * received byte in a task context.
     */
    if (!rx_ev.ev_queued) {
        os_eventq_put(os_eventq_dflt_get(), &rx_ev);
    }

    return 0;
}

static void
uart_init(void)
{
    struct uart_conf uc = {
        .uc_speed    = 9600,
        .uc_databits = 8,
        .uc_stopbits = 1,
        .uc_parity   = UART_PARITY_NONE,
        .uc_flow_ctl = UART_FLOW_CTL_NONE,
        .uc_tx_char  = uart_tx_char_cb,
        .uc_rx_char  = uart_rx_char_cb,
    };

    /* Set up the event for UART RX */
    rx_ev.ev_cb = uart_console_rx_char_event;

    uart_dev = (struct uart_dev *)os_dev_open("uart0", OS_TIMEOUT_NEVER, &uc);
    assert(uart_dev != NULL);

    MODLOG_INFO(LOG_MODULE_MAIN, "UART TLV receiver ready (115200 8N1)\n");
}

/* This is the task function */
void my_task_func(void *arg) {

    /* The task is a forever loop that does not return */
    while (1) {
        /* Wait one second */
        os_time_delay(os_time_ms_to_ticks32(1000));
        /* Print "Hello World!" to the console */
        console_printf("Hello World!\n");
        MODLOG_INFO(LOG_MODULE_MAIN, "Hello World!\n");
    }
}

int
main(int argc, char **argv)
{
    /* Initialize all packages. */
    sysinit();

    console_printf("Hello World Example Application\n");
    MODLOG_INFO(LOG_MODULE_MAIN, "Hello World Example Application");

    uart_init();

    /* Initialize the task */
    int rc = os_task_init(&my_task, "my_task", my_task_func, NULL, MY_TASK_PRI,
                 OS_WAIT_FOREVER, my_task_stack, MY_STACK_SIZE);
    assert(rc == 0);

    /* As the last thing, process events from default event queue. */
    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }

    return 0;
}
