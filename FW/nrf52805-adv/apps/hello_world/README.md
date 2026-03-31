# Apache Mynewt Driver & Logging Reference

This document covers two packages used in Mynewt OS applications:
- **UART Driver** (`hw/drivers/uart`) — serial communication with multiple hardware backends
- **Modular Logging** (`sys/log/modlog`) — module-mapped, level-filtered logging

---

## Table of Contents

- [UART Driver](#uart-driver)
  - [Overview](#overview)
  - [Package Structure](#package-structure)
  - [Core API](#core-api)
    - [Configuration](#configuration)
    - [Callbacks](#callbacks)
    - [Opening a UART Device](#opening-a-uart-device)
    - [Transmitting Data](#transmitting-data)
    - [Receiving Data](#receiving-data)
    - [Flow Control](#flow-control)
  - [Backend Implementations](#backend-implementations)
    - [uart_hal — Hardware UART via HAL](#uart_hal--hardware-uart-via-hal)
    - [uart_bitbang — Software UART via GPIO Bitbanging](#uart_bitbang--software-uart-via-gpio-bitbanging)
    - [uart_da1469x — DA1469x SoC UART](#uart_da1469x--da1469x-soc-uart)
    - [max3107 — SPI-to-UART Bridge](#max3107--spi-to-uart-bridge)
  - [Application Integration](#application-integration)
    - [pkg.yml Dependencies](#pkgyml-dependencies)
    - [syscfg.yml Configuration](#syscfgyml-configuration)
    - [Full Example](#full-example)
  - [Interrupt Safety](#interrupt-safety)
- [Modular Logging (modlog)](#modular-logging-modlog)
  - [Concepts](#concepts)
  - [Core API](#core-api-1)
    - [Data Structures](#data-structures)
    - [Registration and Lifecycle](#registration-and-lifecycle)
    - [Writing Log Entries](#writing-log-entries)
    - [Macros](#macros)
    - [Iteration](#iteration)
  - [Configuration](#configuration-1)
  - [Initialization](#initialization)
  - [Application Integration](#application-integration-1)
    - [pkg.yml Dependency](#pkgyml-dependency)
    - [syscfg.yml Options](#syscfgyml-options)
    - [Multiple Modules with Default Mapping](#multiple-modules-with-default-mapping)
    - [Full Example](#full-example-1)
  - [Advanced Patterns](#advanced-patterns)
    - [Multiple Destinations per Module](#multiple-destinations-per-module)
    - [Default Fallback Mapping](#default-fallback-mapping)
    - [Per-Mapping Level Filtering](#per-mapping-level-filtering)
    - [Dynamic Reconfiguration](#dynamic-reconfiguration)
    - [Hex Dump Logging](#hex-dump-logging)
  - [Thread Safety](#thread-safety)
  - [Disabling Logging at Compile Time](#disabling-logging-at-compile-time)
  - [Return Codes](#return-codes)

---

## UART Driver

This package provides a unified UART driver framework for Apache Mynewt OS. It defines an abstract API that applications use for serial communication, backed by hardware-specific implementations.

### Overview

The UART driver is built around a **callback-driven, interrupt-safe** model:

- The application registers callbacks for TX and RX.
- The driver calls the **TX callback** when the hardware is ready to send the next byte.
- The driver calls the **RX callback** each time a byte is received.
- The driver calls a **TX done callback** when transmission is complete.

All callbacks are invoked from interrupt context (interrupts disabled), so they must execute quickly and must not block.

---

### Package Structure

```
hw/drivers/uart/
├── include/uart/uart.h         # Core abstract API
├── src/uart.c                  # Minimal core implementation
├── pkg.yml
├── uart_hal/                   # HAL-based hardware UART backend
├── uart_bitbang/               # Software UART via GPIO bitbang
├── uart_da1469x/               # DA1469x SoC-specific backend
└── max3107/                    # MAX3107 SPI-to-UART bridge backend
```

---

### Core API

Header: `uart/uart.h`

#### Configuration

```c
/* Parity options */
enum uart_parity {
    UART_PARITY_NONE = 0,
    UART_PARITY_ODD  = 1,
    UART_PARITY_EVEN = 2,
};

/* Hardware flow control options */
enum uart_flow_ctl {
    UART_FLOW_CTL_NONE    = 0,
    UART_FLOW_CTL_RTS_CTS = 1,
};

/* Port-level configuration (baud rate, framing) */
struct uart_conf_port {
    uint32_t         uc_speed;      /* Baud rate in bps, e.g. 115200    */
    uint8_t          uc_databits;   /* Data bits: 5–8                   */
    uint8_t          uc_stopbits;   /* Stop bits: 1–2                   */
    enum uart_parity uc_parity;
    enum uart_flow_ctl uc_flow_ctl;
};

/* Full configuration including application callbacks */
struct uart_conf {
    uint32_t           uc_speed;
    uint8_t            uc_databits;
    uint8_t            uc_stopbits;
    enum uart_parity   uc_parity;
    enum uart_flow_ctl uc_flow_ctl;
    uart_tx_char       uc_tx_char;  /* Called by driver to fetch next TX byte  */
    uart_tx_done       uc_tx_done;  /* Called when TX queue is empty           */
    uart_rx_char       uc_rx_char;  /* Called by driver on each received byte  */
    void              *uc_cb_arg;   /* Passed to all callbacks                 */
};
```

#### Callbacks

```c
/*
 * Called by the driver to fetch the next byte to transmit.
 * Return: byte value (0–255) to send, or -1 if no more data.
 */
typedef int (*uart_tx_char)(void *arg);

/*
 * Called by the driver when the TX queue is fully drained.
 * Interrupts are disabled when this is called.
 */
typedef void (*uart_tx_done)(void *arg);

/*
 * Called by the driver on each received byte.
 * Return: 0 on success, -1 if the RX buffer is full (triggers flow control).
 */
typedef int (*uart_rx_char)(void *arg, uint8_t byte);
```

#### Opening a UART Device

```c
struct os_dev *uart_open(const char *name, struct uart_conf *conf);
```

Opens the UART device registered under `name` (e.g. `"uart0"`) and configures it with `conf`. Returns an `os_dev` pointer cast to `struct uart_dev` on success, or `NULL` on failure.

**Example:**

```c
static struct uart_dev *g_uart;

static int my_tx_char(void *arg) {
    /* Return next byte or -1 */
    return -1;
}

static int my_rx_char(void *arg, uint8_t byte) {
    /* Handle received byte */
    return 0;
}

void my_uart_init(void) {
    struct uart_conf conf = {
        .uc_speed    = 115200,
        .uc_databits = 8,
        .uc_stopbits = 1,
        .uc_parity   = UART_PARITY_NONE,
        .uc_flow_ctl = UART_FLOW_CTL_NONE,
        .uc_tx_char  = my_tx_char,
        .uc_rx_char  = my_rx_char,
        .uc_tx_done  = NULL,
        .uc_cb_arg   = NULL,
    };

    g_uart = (struct uart_dev *)uart_open("uart0", &conf);
    assert(g_uart != NULL);
}
```

#### Transmitting Data

After queueing data into a buffer, notify the driver to start sending:

```c
/* Tell the driver that TX data is available — triggers TX callback polling */
uart_start_tx(struct uart_dev *dev);

/* Send a single byte in a blocking, polling loop (for early boot / logging only) */
uart_blocking_tx(struct uart_dev *dev, uint8_t byte);
```

A typical pattern is to maintain a small ring buffer. The `uc_tx_char` callback dequeues bytes one at a time; your application code enqueues bytes and calls `uart_start_tx()`.

#### Receiving Data

Received bytes are delivered one at a time via the `uc_rx_char` callback. If the application cannot accept a byte (buffer full), return `-1` from the callback. The driver will stall RX and de-assert CTS (if hardware flow control is enabled).

When the application is ready to receive again:

```c
uart_start_rx(struct uart_dev *dev);
```

#### Flow Control

| Mode | Behavior |
|------|----------|
| `UART_FLOW_CTL_NONE` | No flow control |
| `UART_FLOW_CTL_RTS_CTS` | RTS/CTS hardware flow control |

Software-level stalling also works without hardware flow control: returning `-1` from `uc_rx_char` pauses RX interrupt processing until `uart_start_rx()` is called.

---

### Backend Implementations

Choose the backend that matches your hardware by adding it as a dependency in `pkg.yml`.

---

#### `uart_hal` — Hardware UART via HAL

**Package:** `hw/drivers/uart/uart_hal`

The standard backend for MCUs with built-in hardware UART peripherals. It wraps the Mynewt Hardware Abstraction Layer (`hw/hal`).

Device names are mapped to HAL UART IDs by parsing the trailing digit of the device name: `"uart0"` → HAL UART 0, `"uart2"` → HAL UART 2.

**BSP Registration (in your BSP's `bsp.c`):**

```c
#include <uart_hal/uart_hal.h>

static const struct uart_conf_port uart0_conf = {
    /* BSP-specific HAL configuration */
};

OS_DEV_CREATE_PERSISTENT_INST(uart_dev, uart0_dev);

void bsp_init(void) {
    os_dev_create((struct os_dev *)&uart0_dev, "uart0",
                  OS_DEV_INIT_PRIMARY, 0,
                  uart_hal_init, (void *)&uart0_conf);
}
```

**pkg.yml dependency:**

```yaml
pkg.deps:
    - hw/drivers/uart/uart_hal
```

---

#### `uart_bitbang` — Software UART via GPIO Bitbanging

**Package:** `hw/drivers/uart/uart_bitbang`

A fully software-based UART using two GPIO pins and CPU timer bit-timing. Useful when hardware UART peripherals are unavailable or fully occupied.

**Limitations:**
- Maximum baud rate: 19200 bps
- 8N1 frame format only (no configurable parity or stop bits)
- TX pin is required; RX pin is optional

**Configuration in `syscfg.yml`:**

```yaml
syscfg.vals:
    UARTBB_0_PIN_TX: 15        # Required: GPIO pin for TX
    UARTBB_0_PIN_RX: 14        # Optional: GPIO pin for RX (-1 to disable)
    CONSOLE_UART_BAUD: 9600    # Must be <= 19200
```

**Initialization (called automatically via sysinit):**

The package registers the device automatically when `UARTBB_0_PIN_TX >= 0`. The device name is `"uart_bitbang0"` by default.

**Hardware configuration structure (for manual init):**

```c
#include <uart_bitbang/uart_bitbang.h>

static const struct uart_bitbang_conf ubb_conf = {
    .ubc_rxpin        = 14,
    .ubc_txpin        = 15,
    .ubc_cputimer_freq = MYNEWT_VAL(OS_CPUTIME_FREQ),
};

uart_bitbang_init(odev, (void *)&ubb_conf);
```

**pkg.yml dependency:**

```yaml
pkg.deps:
    - hw/drivers/uart/uart_bitbang
```

---

#### `uart_da1469x` — DA1469x SoC UART

**Package:** `hw/drivers/uart/uart_da1469x`

Hardware UART driver for Dialog Semiconductor DA1469x SoCs. Supports three independent UART ports with wake-on-RX capability.

**Device creation:**

```c
#include <uart_da1469x/uart_da1469x.h>

static struct da1469x_uart_dev uart1_dev;

static const struct da1469x_uart_cfg uart1_cfg = {
    .pin_rx = MCU_GPIO_PORT1(8),
    .pin_tx = MCU_GPIO_PORT1(9),
};

void bsp_init(void) {
    da1469x_uart_dev_create(&uart1_dev, "uart1", 0, &uart1_cfg);
}
```

**Wake-on-RX:**

The driver monitors the RX GPIO pin for activity when the UART peripheral is suspended. A configurable debounce delay prevents spurious wakeups.

**syscfg.yml:**

```yaml
syscfg.vals:
    UART_DA1469X_WAKEUP_DELAY_MS: 100   # RX debounce delay in ms (default: 100)
```

**pkg.yml dependency:**

```yaml
pkg.deps:
    - hw/drivers/uart/uart_da1469x
```

---

#### `max3107` — SPI-to-UART Bridge

**Package:** `hw/drivers/uart/max3107`

Driver for the Maxim MAX3107 external UART chip connected over SPI. Adds extra UART ports to MCUs with limited on-chip peripherals. Includes 128-byte hardware TX/RX FIFOs.

This driver has its own read/write API in addition to exposing a standard `uart_dev` interface.

**syscfg.yml options:**

```yaml
syscfg.vals:
    MAX3107_0: 1                       # Enable device instance 0
    MAX3107_SHELL: 0                   # Enable diagnostic shell commands

    # Device instance settings:
    MAX3107_0_NAME: "uart4"            # Name for use with uart_open()
    MAX3107_0_SPI_BUS: "spi0"          # SPI bus device name
    MAX3107_0_CS_PIN: 10               # SPI chip-select GPIO pin
    MAX3107_0_IRQ_PIN: 11              # MAX3107 /IRQ GPIO pin
    MAX3107_0_LDOEN_PIN: -1            # LDO enable pin (-1 if unused)
    MAX3107_0_SPI_BAUDRATE: 4000       # SPI clock in kHz
    MAX3107_0_UART_BAUDRATE: 115200    # UART baud rate in bps
    MAX3107_0_UART_DATA_BITS: 8
    MAX3107_0_UART_PARITY: 0           # 0=none, 1=odd, 2=even
    MAX3107_0_UART_STOP_BITS: 1
    MAX3107_0_UART_FLOW_CONTROL: 0
    MAX3107_0_OSC_FREQ: 0              # External oscillator frequency (0 = internal)
    MAX3107_0_CRYSTAL_EN: 0            # 0 = external clock, 1 = crystal
    MAX3107_0_UART_RX_FIFO_LEVEL: 24   # RX trigger level (multiple of 8, max 128)
    MAX3107_0_UART_TX_FIFO_LEVEL: 64   # TX trigger level (max 128)
```

**Using via standard UART API:**

When `MAX3107_0_NAME` is set to a valid device name (e.g. `"uart4"`), you can use it identically to any other UART device:

```c
g_uart = (struct uart_dev *)uart_open("uart4", &conf);
uart_start_tx(g_uart);
```

**Using via native MAX3107 API:**

For direct FIFO access without the callback model:

```c
#include <max3107/max3107.h>

static struct max3107_dev *g_max;

static const struct max3107_client client = {
    .readable = on_readable,   /* Called when RX data is available  */
    .writable = on_writable,   /* Called when TX FIFO has space      */
    .error    = on_error,
};

void my_max3107_init(void) {
    g_max = max3107_open("max3107_0", &client);
    assert(g_max != NULL);
}

void send_data(const uint8_t *buf, size_t len) {
    int sent = max3107_write(g_max, buf, len);
    /* sent = bytes accepted into FIFO (0 if FIFO full) */
}

void recv_data(uint8_t *buf, size_t len) {
    int n = max3107_read(g_max, buf, len);
    /* n = bytes read (0 if nothing available) */
}
```

**Error codes:**

```c
typedef enum max3107_error {
    MAX3107_ERROR_IO_FAILURE   = 0x01,   /* SPI transaction failed   */
    MAX3107_ERROR_UART_OVERRUN = 0x02,   /* RX FIFO overflowed       */
    MAX3107_ERROR_UART_PARITY  = 0x04,   /* Parity error             */
    MAX3107_ERROR_UART_FRAMING = 0x08,   /* Framing error            */
} max3107_error_t;
```

**pkg.yml dependencies:**

```yaml
pkg.deps:
    - hw/drivers/uart/max3107
    - hw/drivers/uart/max3107/max3107_0   # Device instance 0
```

---

### Application Integration

#### pkg.yml Dependencies

Add one of the following to your application or BSP's `pkg.yml`:

```yaml
pkg.deps:
    - hw/drivers/uart               # Core API (always needed)
    - hw/drivers/uart/uart_hal      # For hardware UART on most MCUs
    # --- OR ---
    - hw/drivers/uart/uart_bitbang  # For software UART via GPIO
    # --- OR ---
    - hw/drivers/uart/uart_da1469x  # For DA1469x SoCs
    # --- OR ---
    - hw/drivers/uart/max3107       # For MAX3107 SPI-UART bridge
    - hw/drivers/uart/max3107/max3107_0
```

#### syscfg.yml Configuration

Refer to the backend sections above. Most settings have sensible defaults. At minimum, configure:
- Baud rate and pin assignments for your hardware
- Device name(s) to match what your application calls `uart_open()` with

#### Full Example

A minimal application that echoes received bytes back over UART:

**pkg.yml:**
```yaml
pkg.name: apps/uart_echo
pkg.deps:
    - kernel/os
    - hw/drivers/uart/uart_hal
```

**syscfg.yml:**
```yaml
syscfg.vals:
    CONSOLE_UART_BAUD: 115200
```

**main.c:**
```c
#include <os/os.h>
#include <uart/uart.h>

static struct uart_dev *g_uart;

/* Small ring buffer */
static uint8_t g_tx_buf[64];
static int g_tx_head, g_tx_tail;

static int tx_char(void *arg) {
    if (g_tx_head == g_tx_tail) {
        return -1;  /* No data */
    }
    uint8_t b = g_tx_buf[g_tx_tail];
    g_tx_tail = (g_tx_tail + 1) % sizeof(g_tx_buf);
    return b;
}

static int rx_char(void *arg, uint8_t byte) {
    int next = (g_tx_head + 1) % sizeof(g_tx_buf);
    if (next == g_tx_tail) {
        return -1;  /* TX buffer full — stall RX */
    }
    g_tx_buf[g_tx_head] = byte;
    g_tx_head = next;
    uart_start_tx(g_uart);
    return 0;
}

int main(void) {
    sysinit();

    struct uart_conf conf = {
        .uc_speed    = 115200,
        .uc_databits = 8,
        .uc_stopbits = 1,
        .uc_parity   = UART_PARITY_NONE,
        .uc_flow_ctl = UART_FLOW_CTL_NONE,
        .uc_tx_char  = tx_char,
        .uc_rx_char  = rx_char,
        .uc_cb_arg   = NULL,
    };

    g_uart = (struct uart_dev *)uart_open("uart0", &conf);
    assert(g_uart != NULL);

    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }
    return 0;
}
```

---

### Interrupt Safety

All three callbacks (`uc_tx_char`, `uc_rx_char`, `uc_tx_done`) are invoked with **interrupts disabled** from the hardware ISR. Follow these rules inside callbacks:

- Do **not** call any blocking OS primitives (`os_sem_pend`, `os_mutex_pend`, `os_time_delay`, etc.)
- Do **not** call `uart_start_tx()` or `uart_start_rx()` directly from within callbacks — schedule work via an `os_event` or `os_callout` instead
- Keep execution time minimal; access only pre-allocated buffers
- Use `uart_blocking_tx()` only during early boot or crash handlers, never from normal application code

---

## Modular Logging (modlog)

`sys/log/modlog` provides a **module-mapped logging** layer that sits on top of Mynewt's core `log` infrastructure. Instead of writing directly to a `struct log`, application code logs by **module ID**. At runtime (or boot time), each module ID is mapped to one or more `struct log` destinations, with an independent minimum-level filter per mapping.

### Concepts

| Term | Meaning |
|------|---------|
| **Module ID** | A `uint8_t` (0–254) that identifies the source of a log entry. 255 is reserved for the default mapping. |
| **Mapping** | A registered association between a module ID, a `struct log *` destination, and a minimum log level. |
| **Handle** | An opaque `uint8_t` returned by `modlog_register()` that identifies a specific mapping. |
| **Default mapping** | A mapping registered with module ID `MODLOG_MODULE_DFLT` (255). Any module that has no explicit mapping falls back to all default mappings. |
| **Min-level filter** | Entries below the mapping's minimum level are silently dropped before reaching the log backend. |

A single module ID can have multiple mappings (e.g. both a RAM circular buffer and a UART console). One `modlog_append()` call fans out to all matching mappings.

---

### Core API

Header: `modlog/modlog.h`

#### Data Structures

```c
/* Describes a single module → log mapping */
struct modlog_desc {
    struct log *log;       /* Destination log object                  */
    uint8_t     handle;    /* Unique handle returned by register()    */
    uint8_t     module;    /* Module ID (0-254, or 255 for default)   */
    uint8_t     min_level; /* Entries below this level are dropped    */
};

/* Special module ID for catch-all default mappings */
#define MODLOG_MODULE_DFLT  255

/* Callback type for modlog_foreach() */
typedef int modlog_foreach_fn(const struct modlog_desc *desc, void *arg);
```

#### Registration and Lifecycle

```c
/*
 * Register a module → log mapping.
 *
 * module    : Module ID to map (0-254, or MODLOG_MODULE_DFLT for default)
 * log       : Destination log object (must not be NULL)
 * min_level : Minimum log level to pass through (e.g. LOG_LEVEL_DEBUG)
 * out_handle: Receives the mapping handle (may be NULL if not needed)
 *
 * Returns: 0 on success, SYS_ENOMEM if max mappings reached, SYS_EINVAL on bad args
 */
int modlog_register(uint8_t module, struct log *log,
                    uint8_t min_level, uint8_t *out_handle);

/*
 * Retrieve mapping info by handle.
 * Returns: 0 on success, SYS_ENOENT if handle not found
 */
int modlog_get(uint8_t handle, struct modlog_desc *out_desc);

/*
 * Delete a single mapping by handle.
 * Returns: 0 on success, SYS_ENOENT if handle not found
 */
int modlog_delete(uint8_t handle);

/*
 * Delete all mappings.
 */
void modlog_clear(void);
```

#### Writing Log Entries

```c
/*
 * Append a flat byte buffer as a log entry.
 *
 * module : Source module ID
 * level  : Log level (e.g. LOG_LEVEL_INFO)
 * etype  : Entry type (e.g. LOG_ETYPE_STRING, LOG_ETYPE_BINARY)
 * data   : Pointer to data
 * len    : Byte length of data
 */
int modlog_append(uint8_t module, uint8_t level,
                  uint8_t etype, void *data, uint16_t len);

/*
 * Append an mbuf chain as a log entry.
 * The mbuf is NOT consumed; the caller retains ownership.
 */
int modlog_append_mbuf(uint8_t module, uint8_t level,
                       uint8_t etype, struct os_mbuf *om);

/*
 * Format and append a printf-style string entry.
 * Output is truncated to MODLOG_MAX_PRINTF_LEN bytes (default: 128).
 */
void modlog_printf(uint8_t module, uint8_t level, const char *msg, ...);

/*
 * Log a hex dump of a buffer, formatted with line breaks.
 * line_break: bytes per line (0 = no line breaks)
 */
void modlog_hexdump(uint8_t module, uint8_t level,
                    const void *data, uint16_t len, uint16_t line_break);
```

#### Macros

The preferred way to log. All macros expand to no-ops when `LOG_FULL` is disabled or when the compile-time `LOG_LEVEL` is too low, incurring zero runtime cost.

```c
/* Level-specific macros — printf-style format string */
MODLOG_DEBUG(module, fmt, ...)
MODLOG_INFO(module, fmt, ...)
MODLOG_WARN(module, fmt, ...)
MODLOG_ERROR(module, fmt, ...)
MODLOG_CRITICAL(module, fmt, ...)

/* Generic macro — level is a literal (DEBUG, INFO, WARN, ERROR, CRITICAL) */
MODLOG(level, module, fmt, ...)

/* Hex dump variants */
MODLOG_HEXDUMP_DEBUG(module, data_ptr, len, line_break)
MODLOG_HEXDUMP_INFO(module, data_ptr, len, line_break)
MODLOG_HEXDUMP_WARN(module, data_ptr, len, line_break)
MODLOG_HEXDUMP_ERROR(module, data_ptr, len, line_break)
MODLOG_HEXDUMP_CRITICAL(module, data_ptr, len, line_break)
MODLOG_HEXDUMP(level, module, data_ptr, len, line_break)

/* Convenience macro — logs to the default module (255) */
MODLOG_DFLT(level, fmt, ...)
MODLOG_HEXDUMP_DFLT(level, data_ptr, len, line_break)
```

#### Iteration

```c
/*
 * Call fn(desc, arg) for every registered mapping.
 * fn may safely call modlog_delete() on the mapping being visited.
 * Return nonzero from fn to stop early.
 */
int modlog_foreach(modlog_foreach_fn *fn, void *arg);
```

---

### Configuration

All options are in `sys/log/modlog/syscfg.yml`:

| Option | Default | Description |
|--------|---------|-------------|
| `MODLOG_MAX_MAPPINGS` | `16` | Maximum simultaneous module-to-log mappings. Each costs ~12 bytes of RAM. |
| `MODLOG_MAX_PRINTF_LEN` | `128` | Maximum length (bytes) of a formatted string after `%` expansion. Longer output is truncated. |
| `MODLOG_CONSOLE_DFLT` | `1` | When enabled, automatically registers the console log as the default mapping (module 255, DEBUG level) at sysinit. |
| `MODLOG_LOG_MACROS` | `0` | When enabled, redefines the legacy `LOG_DEBUG` / `LOG_INFO` / etc. macros to route through modlog. Useful for migrating existing code. |
| `MODLOG_SYSINIT_STAGE` | `100` | Sysinit stage at which `modlog_init()` runs. Increase if your log backends initialize after stage 100. |

---

### Initialization

modlog initializes itself automatically via the Mynewt sysinit mechanism — no explicit call is needed in application code.

```yaml
# pkg.yml excerpt (already set by the modlog package itself)
pkg.init:
    modlog_init: 'MYNEWT_VAL(MODLOG_SYSINIT_STAGE)'
```

At init time (`modlog_init()`):
1. A mempool for `MODLOG_MAX_MAPPINGS` mapping entries is created.
2. The internal mapping list and read-write lock are initialized.
3. If `MODLOG_CONSOLE_DFLT=1`, the console log is registered as the default destination.

---

### Application Integration

#### pkg.yml Dependency

```yaml
pkg.deps:
    - sys/log/modlog
```

#### syscfg.yml Options

```yaml
syscfg.vals:
    MODLOG_MAX_MAPPINGS: 16          # Increase if you need more module mappings
    MODLOG_MAX_PRINTF_LEN: 128       # Increase for long formatted messages
    MODLOG_CONSOLE_DFLT: 1           # 1 = auto-register console as default log
    MODLOG_LOG_MACROS: 0             # 1 = make LOG_DEBUG/INFO/... use modlog
    MODLOG_SYSINIT_STAGE: 100
```

#### Multiple Modules with Default Mapping

The simplest way to add per-subsystem logging to `main.c` is to define module ID constants and use them directly in the macros. With `MODLOG_CONSOLE_DFLT=1` (the default), all modules that have no explicit registration automatically route to the console — no setup code required.

```c
#include <modlog/modlog.h>

/* Define module IDs for each subsystem — any values 0-254 */
#define MOD_MAIN    1
#define MOD_SENSOR  2
#define MOD_BLE     3

int main(void) {
    sysinit();

    MODLOG_INFO(MOD_MAIN, "App started");

    sensor_init();
    ble_init();

    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }
}

void sensor_init(void) {
    MODLOG_DEBUG(MOD_SENSOR, "Sensor init");
    MODLOG_INFO(MOD_SENSOR, "Sensor ready");
}

void ble_init(void) {
    MODLOG_DEBUG(MOD_BLE, "BLE init");
    MODLOG_INFO(MOD_BLE, "BLE ready");
}
```

All three modules fall through to the default console mapping. The module ID is embedded in each log entry so entries from different subsystems can be distinguished.

To give **one specific module** a different log level while leaving all others on the default, register only that one explicitly:

```c
/* BLE logs at DEBUG; all other modules stay at the default console level */
modlog_register(MOD_BLE, log_console_get(), LOG_LEVEL_DEBUG, NULL);
```

Modules with an explicit registration use that mapping. Modules without one continue to fall back to the default.

Available level constants, from least to most severe:

| Constant | Value |
|----------|-------|
| `LOG_LEVEL_DEBUG` | 0 |
| `LOG_LEVEL_INFO` | 1 |
| `LOG_LEVEL_WARN` | 2 |
| `LOG_LEVEL_ERROR` | 3 |
| `LOG_LEVEL_CRITICAL` | 4 |

#### Full Example

```c
#include <modlog/modlog.h>
#include <log/log.h>

#define MOD_SENSOR  1
#define MOD_COMM    2

/* A log object backed by a circular buffer in RAM */
static struct log g_sensor_log;

void app_log_init(void) {
    /* Initialize the underlying log backend (e.g. cbmem) */
    log_register("sensor", &g_sensor_log, &log_cbmem_handler,
                 &g_cbmem, LOG_SYSLEVEL);

    /* Map MOD_SENSOR to g_sensor_log, passing DEBUG and above */
    modlog_register(MOD_SENSOR, &g_sensor_log, LOG_LEVEL_DEBUG, NULL);

    /*
     * MOD_COMM has no explicit mapping, so it falls back to the
     * default console mapping registered by MODLOG_CONSOLE_DFLT.
     */
}

void sensor_read(void) {
    int value = read_adc();

    MODLOG_DEBUG(MOD_SENSOR, "ADC raw = %d", value);

    if (value < 0) {
        MODLOG_ERROR(MOD_SENSOR, "ADC read failed: %d", value);
    }
}

void comm_send(const uint8_t *pkt, uint16_t len) {
    /* No explicit mapping — goes to console via default */
    MODLOG_INFO(MOD_COMM, "TX %u bytes", len);
    MODLOG_HEXDUMP_DEBUG(MOD_COMM, pkt, len, 16);
}
```

---

### Advanced Patterns

#### Multiple Destinations per Module

A single module can fan out to multiple log backends simultaneously:

```c
uint8_t h_ram, h_uart;

modlog_register(MOD_SENSOR, &g_ram_log,  LOG_LEVEL_DEBUG, &h_ram);
modlog_register(MOD_SENSOR, &g_uart_log, LOG_LEVEL_WARN,  &h_uart);

/* One write hits both logs.
 * g_ram_log receives DEBUG and above.
 * g_uart_log receives WARN and above only. */
MODLOG_DEBUG(MOD_SENSOR, "verbose trace");   /* → ram only */
MODLOG_WARN(MOD_SENSOR,  "something wrong"); /* → ram + uart */
```

#### Default Fallback Mapping

Any module without an explicit mapping routes to all default mappings:

```c
/* Register console as default for all unmapped modules */
modlog_register(MODLOG_MODULE_DFLT, log_console_get(),
                LOG_LEVEL_INFO, NULL);

/* Both go to the console (no explicit mapping for 42 or 99) */
MODLOG_INFO(42, "from module 42");
MODLOG_INFO(99, "from module 99");
```

#### Per-Mapping Level Filtering

```c
/* Only forward WARN and above to the UART log */
modlog_register(MY_MODULE, &g_uart_log, LOG_LEVEL_WARN, &handle);

MODLOG_DEBUG(MY_MODULE, "dropped silently");  /* below WARN — discarded */
MODLOG_WARN(MY_MODULE,  "forwarded");         /* at or above WARN — written */
```

#### Dynamic Reconfiguration

Mappings can be added, removed, or replaced at runtime:

```c
/* Remove the old mapping */
modlog_delete(old_handle);

/* Register a new one with a higher threshold */
modlog_register(MY_MODULE, &g_new_log, LOG_LEVEL_ERROR, &new_handle);

/* Or remove everything and start over */
modlog_clear();
```

#### Hex Dump Logging

```c
uint8_t packet[32] = { /* ... */ };

/* Dump with 16 bytes per line at DEBUG level */
MODLOG_HEXDUMP_DEBUG(MY_MODULE, packet, sizeof(packet), 16);

/* Or call the function directly */
modlog_hexdump(MY_MODULE, LOG_LEVEL_INFO, packet, sizeof(packet), 8);
```

---

### Thread Safety

All modlog operations are protected by an internal read-write lock:

- **Read lock** (shared): `modlog_append`, `modlog_append_mbuf`, `modlog_printf`, `modlog_hexdump`, `modlog_get`, `modlog_foreach`
- **Write lock** (exclusive): `modlog_register`, `modlog_delete`, `modlog_clear`

It is safe to call `modlog_append` / macros concurrently from multiple tasks or from interrupt context. `modlog_delete()` may be called from within a `modlog_foreach()` callback on the mapping currently being visited.

---

### Disabling Logging at Compile Time

All modlog code is guarded by `#if MYNEWT_VAL(LOG_FULL)`. When `LOG_FULL` is disabled (e.g. for minimal production builds):

- All write macros (`MODLOG_DEBUG`, etc.) expand to nothing.
- `modlog_append` / `modlog_printf` / `modlog_hexdump` become no-ops that return 0.
- `modlog_register` returns 0 without allocating anything.
- `modlog_get` / `modlog_delete` / `modlog_foreach` return `SYS_ENOTSUP`.

This means logging calls can be left in application code with zero runtime overhead when logging is compiled out.

---

### Return Codes

| Code | Meaning |
|------|---------|
| `0` | Success |
| `SYS_ENOENT` | Handle or mapping not found |
| `SYS_EINVAL` | Invalid argument (e.g. NULL log pointer) |
| `SYS_ENOMEM` | Mapping pool exhausted (`MODLOG_MAX_MAPPINGS` reached) |
| `SYS_EIO` | Underlying log write failed |
| `SYS_ENOTSUP` | Operation not available (`LOG_FULL` disabled) |
