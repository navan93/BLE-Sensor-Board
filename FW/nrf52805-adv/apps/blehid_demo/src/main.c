#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "os/os.h"
#include "config/config.h"
#include "sysinit/sysinit.h"
#include "log/log.h"
#include "host/ble_att.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_mbuf.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "console/console.h"
#include "uart/uart.h"

/**
 * Depending on the type of package, there are different
 * compilation rules for this directory.  This comment applies
 * to packages of type "app."  For other types of packages,
 * please view the documentation at http://mynewt.apache.org/.
 *
 * Put source files in this directory.  All files that have a *.c
 * ending are recursively compiled in the src/ directory and its
 * descendants.  The exception here is the arch/ directory, which
 * is ignored in the default compilation.
 *
 * The arch/<your-arch>/ directories are manually added and
 * recursively compiled for all files that end with either *.c
 * or *.a.  Any directories in arch/ that don't match the
 * architecture being compiled are not compiled.
 *
 * Architecture is set by the BSP/MCU combination.
 */

#define OUTPUT_REPORT_MAX_LEN            1
#define INPUT_REP_KEYS_REF_ID            0
#define OUTPUT_REP_KEYS_REF_ID           0
#define MODIFIER_KEY_POS                 0
#define SHIFT_KEY_CODE                   0x02
#define SCAN_CODE_POS                    2
/* ********************* */
/* Buttons configuration */

/* Note: The configuration below is the same as BOOT mode configuration
 * This simplifies the code as the BOOT mode is the same as REPORT mode.
 * Changing this configuration would require separate implementation of
 * BOOT mode report generation.
 */
#define KEY_CTRL_CODE_MIN               224 /* Control key codes - required 8 of them */
#define KEY_CTRL_CODE_MAX               231 /* Control key codes - required 8 of them */
#define KEY_CODE_MIN                    0   /* Normal key codes */
#define KEY_CODE_MAX                    101 /* Normal key codes */
#define KEY_PRESS_MAX                   6   /* Maximum number of non-control keys
			                                 * pressed simultaneously
			                                 */
/* Number of bytes in key report
 *
 * 1B - control keys
 * 1B - reserved
 * rest - non-control keys
 */
#define INPUT_REPORT_KEYS_MAX_LEN       (1 + 1 + KEY_PRESS_MAX)
#define KEYS_MAX_LEN                    (INPUT_REPORT_KEYS_MAX_LEN - \
					                    SCAN_CODE_POS)


/* HID keyboard modifier bits (byte 0 of report) */
#define HID_MOD_LEFT_CTRL   (1 << 0)
#define HID_MOD_LEFT_SHIFT  (1 << 1)
#define HID_MOD_LEFT_ALT    (1 << 2)
#define HID_MOD_LEFT_GUI    (1 << 3)  /* Windows/Command key */
#define HID_MOD_RIGHT_CTRL  (1 << 4)
#define HID_MOD_RIGHT_SHIFT (1 << 5)
#define HID_MOD_RIGHT_ALT   (1 << 6)
#define HID_MOD_RIGHT_GUI   (1 << 7)

static const char *device_name = "Sensor Watch F91-W";

/**
 * HID keyboard input structure.
 * Represents a single HID keyboard report (up to 6 simultaneous keys + modifiers).
 */
struct hid_keyboard_input {
    uint8_t modifiers;      /* Bitfield of HID_MOD_* flags */
    uint8_t keycodes[6];    /* Up to 6 simultaneous keycodes */
};

/* HID keyboard state */
static uint16_t hid_input_report_handle;
static uint8_t hid_input_report[INPUT_REPORT_KEYS_MAX_LEN];
static uint8_t hid_output_report;
static uint8_t hid_protocol_mode = 0x01;
static uint8_t hid_control_point;
static uint16_t ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static int hid_notify_enabled;

/* CTS (Current Time Service) client state */
static const ble_uuid16_t cts_svc_uuid = BLE_UUID16_INIT(0x1805);
static const ble_uuid16_t cts_chr_uuid = BLE_UUID16_INIT(0x2A2B);
static uint16_t cts_chr_handle;

/* Battery Service state */
static uint8_t battery_level = 100; /* percentage */

/* Device Information Service data */
static const char *dis_manufacturer_name = "Sensor Watch";
static const char *dis_model_number      = "F91-W";
/* PnP ID: Vendor ID Source=0x02 (USB-IF), Vendor ID=0x1915 (Nordic),
 *         Product ID=0x0001, Product Version=0x0100 */
static const uint8_t dis_pnp_id[] = {
    0x02,             /* Vendor ID Source: USB Implementers Forum */
    0x15, 0x19,       /* Vendor ID: 0x1915 (Nordic Semiconductor) LE */
    0xEF, 0xEE,       /* Product ID: 0x0001 LE */
    0x00, 0x01        /* Product Version: 0x0100 LE */
};

static const uint8_t hid_input_report_ref[] = { 0x00, 0x01 }; /* ID=0, Type=Input */
static const uint8_t hid_external_report_ref[] = { 0x00, 0x00 };

/* HID descriptors */
static const uint8_t hid_info[] = {
    0x11, 0x01, /* bcdHID (1.11) */
    0x00,       /* bCountryCode (0 = not localized) */
    0x02        /* flags: normally connectable */
};

/* Simple 8-byte keyboard report (modifiers + reserved + 6 keycodes) */
static const uint8_t hid_report_map[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x06,       /* Usage (Keyboard) */
    0xA1, 0x01,       /* Collection (Application) */

    /* ---- Input Report (ID = 1) ---- */
#if INPUT_REP_KEYS_REF_ID
		0x85, INPUT_REP_KEYS_REF_ID,    /*   Report ID (1) */
#endif

    /* Modifier byte */
    0x05, 0x07,       /*   Usage Page (Key Codes) */
    0x19, 0xE0,       /*   Usage Minimum (224 = Left Ctrl) */
    0x29, 0xE7,       /*   Usage Maximum (231 = Right GUI) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x25, 0x01,       /*   Logical Maximum (1) */
    0x75, 0x01,       /*   Report Size (1) */
    0x95, 0x08,       /*   Report Count (8) */
    0x81, 0x02,       /*   Input (Data, Var, Abs) – modifier bits */

    /* Reserved byte */
    0x95, 0x01,       /*   Report Count (1) */
    0x75, 0x08,       /*   Report Size (8) */
    0x81, 0x01,       /*   Input (Const, Array, Abs) – reserved */

    /* 6 Keycode bytes */
    0x95, 0x06,       /*   Report Count (6) */
    0x75, 0x08,       /*   Report Size (8) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x25, 0x65,       /*   Logical Maximum (101) */
    0x05, 0x07,       /*   Usage Page (Key Codes) */
    0x19, 0x00,       /*   Usage Minimum (0) */
    0x29, 0x65,       /*   Usage Maximum (101) */
    0x81, 0x00,       /*   Input (Data, Array, Abs) – 6 keycodes */

    0xC0              /* End Collection */
};

/* Characteristic User Description strings for DIS */
static const char dis_mfr_name_dsc[]  = "Manufacturer Name String";
static const char dis_model_num_dsc[] = "Model Number String";
static const char dis_pnp_id_dsc[]    = "PnP ID";

/* Console input state */
static struct console_input console_buf;
static struct os_event console_event;
static void console_input_cb(struct os_event *ev);

/* ---- UART TLV receiver ---- */
#define TLV_MAX_VALUE_LEN   16

/* Command types */
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

static struct uart_dev     *uart_dev;
static struct tlv_frame     tlv_rx;         /* filled by ISR  */
static struct tlv_frame     tlv_pending;    /* ready for task */
static uint8_t              tlv_idx;
static enum tlv_parse_state tlv_state;
static volatile bool        tlv_frame_ready;
static struct os_event      uart_rx_event;

/* TX: small buffer for outgoing response frames */
static uint8_t              uart_tx_buf[8];
static uint8_t              uart_tx_len;
static volatile uint8_t     uart_tx_idx;

/* Forward declarations */
static void ble_advertise(void);
static void cts_discover(uint16_t conn_handle);
static int gap_event(struct ble_gap_event *event, void *arg);
static int hid_gatt_init(void);
static void hid_send_key(const struct hid_keyboard_input *input);
static int hid_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg);
static int dis_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg);
static int dis_dsc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg);
static int bas_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg);
static void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt,
                                 void *arg);

/**
 * Convert an ASCII character to a HID keycode + modifier.
 * Returns the HID keycode via *keycode and modifier via *mod.
 * Returns 0 on success, -1 if the character is not mappable.
 */
static int
ascii_to_hid(char c, uint8_t *keycode, uint8_t *mod)
{
    *mod = 0;

    if (c >= 'a' && c <= 'z') {
        *keycode = 0x04 + (c - 'a');
    } else if (c >= 'A' && c <= 'Z') {
        *keycode = 0x04 + (c - 'A');
        *mod = HID_MOD_LEFT_SHIFT;
    } else if (c >= '1' && c <= '9') {
        *keycode = 0x1E + (c - '1');
    } else if (c == '0') {
        *keycode = 0x27;
    } else if (c == '\n' || c == '\r') {
        *keycode = 0x28; /* Enter */
    } else if (c == ' ') {
        *keycode = 0x2C; /* Space */
    } else if (c == '-') {
        *keycode = 0x2D;
    } else if (c == '=') {
        *keycode = 0x2E;
    } else if (c == '.') {
        *keycode = 0x37;
    } else if (c == ',') {
        *keycode = 0x36;
    } else {
        return -1;
    }
    return 0;
}

/* GATT service definitions: HID, DIS, and Battery */
static const struct ble_gatt_svc_def hid_gatt_svcs[] = {
    { /*** HID Service (0x1812) ***/
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4A), /* HID Information */
                .access_cb = hid_access_cb,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4E), /* Protocol Mode */
                .access_cb = hid_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4C), /* HID Control Point */
                .access_cb = hid_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4B), /* Report Map */
                .access_cb = hid_access_cb,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2907), /* HID Report Reference Descriptor */
                        .att_flags = BLE_ATT_F_READ | BLE_ATT_F_READ_ENC,
                        .access_cb = hid_access_cb,
                        .arg = (void *)hid_external_report_ref,
                    },
                    { 0 }
                },
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4D), /* Input Report */
                .access_cb = hid_access_cb,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2908), /* HID Report Reference Descriptor */
                        .att_flags = BLE_ATT_F_READ | BLE_ATT_F_READ_ENC,
                        .access_cb = hid_access_cb,
                        .arg = (void *)hid_input_report_ref,
                    },
                    { 0 }
                },
                .flags = BLE_GATT_CHR_F_READ   |
                         BLE_GATT_CHR_F_NOTIFY |
                         BLE_GATT_CHR_F_READ_ENC,
                .val_handle = &hid_input_report_handle,
            },
            { 0 }
        },
    },
    { /*** Device Information Service (0x180A) ***/
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180A),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0x2A29), /* Manufacturer Name */
                .access_cb = dis_access_cb,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2901),
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = dis_dsc_access_cb,
                        .arg = (void *)dis_mfr_name_dsc,
                    },
                    { 0 }
                },
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A24), /* Model Number */
                .access_cb = dis_access_cb,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2901),
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = dis_dsc_access_cb,
                        .arg = (void *)dis_model_num_dsc,
                    },
                    { 0 }
                },
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A50), /* PnP ID */
                .access_cb = dis_access_cb,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2901),
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = dis_dsc_access_cb,
                        .arg = (void *)dis_pnp_id_dsc,
                    },
                    { 0 }
                },
                .flags = BLE_GATT_CHR_F_READ,
            },
            { 0 }
        },
    },
    { /*** Battery Service (0x180F) ***/
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0x2A19), /* Battery Level */
                .access_cb = bas_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }
        },
    },
    { 0 } /* No more services */
};

static void
set_ble_addr(void)
{
    int rc;
    ble_addr_t addr;

    /* Generate a static random address (type 0).
     * NRPA (type 1) changes every boot and breaks bonding. */
    rc = ble_hs_id_gen_rnd(0, &addr);
    assert(rc == 0);

    /* set generated address */
    rc = ble_hs_id_set_rnd(addr.val);
    assert(rc == 0);
}

static int
hid_attr_write(struct os_mbuf *om, uint16_t min_len, uint16_t max_len,
               void *dst, uint16_t *len)
{
    uint16_t om_len;
    int rc;

    om_len = OS_MBUF_PKTLEN(om);
    if (om_len < min_len || om_len > max_len) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    rc = ble_hs_mbuf_to_flat(om, dst, max_len, len);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    return 0;
}

static int
hid_access_cb(uint16_t conn_handle, uint16_t attr_handle,
              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t uuid16;
    int rc;

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        uuid16 = ble_uuid_u16(ctxt->chr->uuid);
        switch (uuid16) {
        case 0x2A4A: /* HID Information */
            rc = os_mbuf_append(ctxt->om, hid_info, sizeof(hid_info));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        case 0x2A4E: /* Protocol Mode */
            rc = os_mbuf_append(ctxt->om, &hid_protocol_mode,
                                sizeof(hid_protocol_mode));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        case 0x2A4B: /* Report Map */
            rc = os_mbuf_append(ctxt->om, hid_report_map,
                                sizeof(hid_report_map));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        case 0x2A4D: /* Input Report */
            if (attr_handle == hid_input_report_handle) {
                rc = os_mbuf_append(ctxt->om, hid_input_report,
                                    sizeof(hid_input_report));
                return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            return BLE_ATT_ERR_UNLIKELY;
        default:
            return BLE_ATT_ERR_UNLIKELY;
        }

    case BLE_GATT_ACCESS_OP_READ_DSC:
        uuid16 = ble_uuid_u16(ctxt->dsc->uuid);
        if ((uuid16 == 0x2908 || uuid16 == 0x2907) && arg != NULL) {
            rc = os_mbuf_append(ctxt->om, arg, 2);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return BLE_ATT_ERR_UNLIKELY;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        uuid16 = ble_uuid_u16(ctxt->chr->uuid);
        switch (uuid16) {
        case 0x2A4C: /* HID Control Point */
            return hid_attr_write(ctxt->om, sizeof(hid_control_point),
                                  sizeof(hid_control_point),
                                  &hid_control_point, NULL);
        case 0x2A4E: /* Protocol Mode */
            return hid_attr_write(ctxt->om, sizeof(hid_protocol_mode),
                                  sizeof(hid_protocol_mode),
                                  &hid_protocol_mode, NULL);
        default:
            return BLE_ATT_ERR_UNLIKELY;
        }

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int
dis_access_cb(uint16_t conn_handle, uint16_t attr_handle,
              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t uuid16;
    int rc;

    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uuid16 = ble_uuid_u16(ctxt->chr->uuid);
    switch (uuid16) {
    case 0x2A29: /* Manufacturer Name */
        rc = os_mbuf_append(ctxt->om, dis_manufacturer_name,
                            strlen(dis_manufacturer_name));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    case 0x2A24: /* Model Number */
        rc = os_mbuf_append(ctxt->om, dis_model_number,
                            strlen(dis_model_number));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    case 0x2A50: /* PnP ID */
        rc = os_mbuf_append(ctxt->om, dis_pnp_id, sizeof(dis_pnp_id));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int
dis_dsc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                  struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const char *desc = (const char *)arg;
    int rc;

    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_DSC || desc == NULL) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    rc = os_mbuf_append(ctxt->om, desc, strlen(desc));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}


static int
bas_access_cb(uint16_t conn_handle, uint16_t attr_handle,
              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;

    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    rc = os_mbuf_append(ctxt->om, &battery_level, sizeof(battery_level));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}


static void
gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        MODLOG_DFLT(DEBUG, "registered service %s with handle=%d\n",
                    ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                    ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        MODLOG_DFLT(DEBUG, "registering characteristic %s with "
                           "def_handle=%d val_handle=%d\n",
                    ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                    ctxt->chr.def_handle,
                    ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        MODLOG_DFLT(DEBUG, "registering descriptor %s with handle=%d\n",
                    ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                    ctxt->dsc.handle);
        break;

    default:
        assert(0);
        break;
    }
}

static int
hid_gatt_init(void)
{
    int rc;

    rc = ble_gatts_count_cfg(hid_gatt_svcs);
    if (rc != 0) {
        return rc;
    }

    rc = ble_gatts_add_svcs(hid_gatt_svcs);
    if (rc != 0) {
        return rc;
    }

    /* Start with no keys pressed */
    memset(hid_input_report, 0, sizeof(hid_input_report));
    hid_output_report = 0;
    hid_protocol_mode = 0x01; // Check
    hid_control_point = 0;

    return 0;
}

/**
 * Send HID keyboard input to the host.
 * Sends a key press followed by a key release.
 *
 * @param input Pointer to hid_keyboard_input structure containing modifiers and keycodes.
 *              Can contain up to 6 simultaneous keycodes (for chords like Ctrl+Alt+Del).
 *The format used for keyboard reports is the following byte array: [modifier, reserved, Key1, Key2, Key3, Key4, Key5, Key6].
 * Example usage:
 *   // Send 'a'
 *   struct hid_keyboard_input key_a = { .modifiers = 0, .keycodes = {0x04} };
 *   hid_send_key(&key_a);
 *
 *   // Send Ctrl+C
 *   struct hid_keyboard_input ctrl_c = {
 *       .modifiers = HID_MOD_LEFT_CTRL,
 *       .keycodes = {0x06}  // 'c'
 *   };
 *   hid_send_key(&ctrl_c);
 */
static void
hid_send_key(const struct hid_keyboard_input *input)
{
    int rc;
    struct os_mbuf *om;

    if (ble_conn_handle == BLE_HS_CONN_HANDLE_NONE || !hid_notify_enabled) {
        return;
    }

    /* Build and send key press report */
    memset(hid_input_report, 0, sizeof(hid_input_report));
    hid_input_report[0] = input->modifiers;
    hid_input_report[1] = 0; // reserved (always 0)
    memcpy(&hid_input_report[2], input->keycodes, 6);

    om = ble_hs_mbuf_from_flat(hid_input_report, sizeof(hid_input_report));
    if (!om) {
        MODLOG_DFLT(ERROR, "Failed to alloc mbuf for key press\n");
        return;
    }

    rc = ble_gatts_notify_custom(ble_conn_handle, hid_input_report_handle, om);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "Failed to send key press; rc=%d\n", rc);
        return;
    }

    /* Small delay to let the controller drain the previous notification */
    os_time_delay(os_time_ms_to_ticks32(10));

    /* Send key release (all zeros) */
    memset(hid_input_report, 0, sizeof(hid_input_report));

    om = ble_hs_mbuf_from_flat(hid_input_report, sizeof(hid_input_report));
    if (!om) {
        MODLOG_DFLT(ERROR, "Failed to alloc mbuf for key release\n");
        return;
    }

    rc = ble_gatts_notify_custom(ble_conn_handle, hid_input_report_handle, om);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "Failed to send key release; rc=%d\n", rc);
    }
}

/* ---- CTS GATT client ---- */

static int
cts_read_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
            struct ble_gatt_attr *attr, void *arg)
{
    uint8_t buf[10];
    uint16_t len = 0;
    uint16_t year;

    if (error->status != 0) {
        MODLOG_DFLT(ERROR, "CTS read error; status=%d\n", error->status);
        return 0;
    }
    if (OS_MBUF_PKTLEN(attr->om) < 7) {
        MODLOG_DFLT(ERROR, "CTS: short response (%d B)\n",
                    OS_MBUF_PKTLEN(attr->om));
        return 0;
    }

    ble_hs_mbuf_to_flat(attr->om, buf, sizeof(buf), &len);
    year = (uint16_t)(buf[0] | ((uint16_t)buf[1] << 8));
    MODLOG_DFLT(INFO, "Current time: %04u-%02u-%02u %02u:%02u:%02u\n",
                year, buf[2], buf[3], buf[4], buf[5], buf[6]);
    return 0;
}

static int
cts_chr_disc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                const struct ble_gatt_chr *chr, void *arg)
{
    if (error->status == BLE_HS_EDONE) {
        if (cts_chr_handle == 0) {
            MODLOG_DFLT(INFO, "CTS: current time chr not found\n");
        }
        return 0;
    }
    if (error->status != 0) {
        MODLOG_DFLT(ERROR, "CTS chr disc error; status=%d\n", error->status);
        return 0;
    }

    cts_chr_handle = chr->val_handle;
    MODLOG_DFLT(INFO, "CTS chr found; handle=%d — reading\n", cts_chr_handle);
    ble_gattc_read(conn_handle, cts_chr_handle, cts_read_cb, NULL);
    return 0;
}

static int
cts_svc_disc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                const struct ble_gatt_svc *svc, void *arg)
{
    if (error->status == BLE_HS_EDONE) {
        return 0;
    }
    if (error->status != 0) {
        MODLOG_DFLT(ERROR, "CTS svc disc error; status=%d\n", error->status);
        return 0;
    }

    MODLOG_DFLT(INFO, "CTS svc found; start=%d end=%d\n",
                svc->start_handle, svc->end_handle);
    ble_gattc_disc_chrs_by_uuid(conn_handle,
                                 svc->start_handle, svc->end_handle,
                                 &cts_chr_uuid.u,
                                 cts_chr_disc_cb, NULL);
    return 0;
}

static void
cts_discover(uint16_t conn_handle)
{
    cts_chr_handle = 0;
    MODLOG_DFLT(INFO, "Starting CTS discovery\n");
    ble_gattc_disc_svc_by_uuid(conn_handle, &cts_svc_uuid.u,
                                cts_svc_disc_cb, NULL);
}

static int
gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        MODLOG_DFLT(INFO, "Connection %s; status=%d\n",
                    event->connect.status == 0 ? "established" : "failed",
                    event->connect.status);
        if (event->connect.status == 0) {
            ble_conn_handle = event->connect.conn_handle;
            /* Initiate security (pairing/bonding) so the link gets encrypted.
             * HID hosts require an encrypted link to accept input reports. */
            int sec_rc = ble_gap_security_initiate(event->connect.conn_handle);
            MODLOG_DFLT(INFO, "Security initiate rc=%d\n", sec_rc);
        } else {
            ble_advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        MODLOG_DFLT(INFO, "Disconnect; reason=%d\n",
                    event->disconnect.reason);
        ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        hid_notify_enabled = 0;
        cts_chr_handle = 0;
        ble_advertise();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        MODLOG_DFLT(INFO, "Encryption change; status=%d\n",
                    event->enc_change.status);
        if (event->enc_change.status == 0) {
            cts_discover(event->enc_change.conn_handle);
        }
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        /* Delete the old bond and allow re-pairing */
        struct ble_gap_conn_desc desc;
        ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        ble_store_util_delete_peer(&desc.peer_id_addr);
        MODLOG_DFLT(INFO, "Repeat pairing; deleted old bond\n");
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == hid_input_report_handle) {
            hid_notify_enabled = event->subscribe.cur_notify;
            MODLOG_DFLT(INFO, "HID input notify=%d\n", hid_notify_enabled);
            if (hid_notify_enabled) {
                MODLOG_DFLT(INFO, "Ready! Type text in RTT console to send as HID keys\n");
            }
        }
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        MODLOG_DFLT(INFO, "Advertising completed, reason: %d\n",
                    event->adv_complete.reason);
        ble_advertise();
        return 0;

    default:
        return 0;
    }
}

/* Manufacturer Specific Data (AD type 0xFF):
 * Company ID = 0x0059 (Nordic Semiconductor) in little-endian,
 * followed by application-specific payload. */
static const uint8_t adv_mfg_data[] = {
    0x59, 0x00,       /* Company ID: Nordic Semiconductor (LE) */
    0x01,             /* Product type: keyboard */
    0x00              /* Flags / reserved */
};

static void
ble_advertise(void)
{
    int rc;
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    struct ble_hs_adv_fields rsp_fields;

    /* set adv parameters */
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min  = 48;   /* 30ms  (48 * 0.625ms) – fast for discovery */
    adv_params.itvl_max  = 160;  /* 100ms (160 * 0.625ms) */

    /*
     * Primary advertising packet:
     *   Flags (3) + Appearance (4) + UUID16 (4) + Mfg Data (2+4=6) = 17 bytes
     */
    memset(&fields, 0, sizeof(fields));

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    /* Appearance = Keyboard (0x03C1 = 961) — tells iOS this is a keyboard */
    fields.appearance = 0x03C1;
    fields.appearance_is_present = 1;

    /* Advertise the HID service UUID (0x1812) so centrals can filter for HID */
    fields.uuids16 = (ble_uuid16_t[]){ BLE_UUID16_INIT(0x1812) };
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    /* Manufacturer Specific Data */
    fields.mfg_data = adv_mfg_data;
    fields.mfg_data_len = sizeof(adv_mfg_data);

    rc = ble_gap_adv_set_fields(&fields);
    assert(rc == 0);

    /*
     * Scan response: device name
     *   Name "Sensor Watch F91-W" (2+18=20 bytes)
     */
    memset(&rsp_fields, 0, sizeof(rsp_fields));
    rsp_fields.name = (uint8_t *)device_name;
    rsp_fields.name_len = strlen(device_name);
    rsp_fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    assert(rc == 0);

    MODLOG_DFLT(INFO, "Starting advertising...\n");

    /* As own address type we use hard-coded value, because we generate
       a static random address and by definition it's random */
    rc = ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event, NULL);
    assert(rc == 0);
}

static void
on_sync(void)
{
    set_ble_addr();

    /* begin advertising */
    ble_advertise();
}

static void
on_reset(int reason)
{
    MODLOG_DFLT(INFO, "Resetting state; reason=%d\n", reason);
}
/**
 * Console line input callback.
 * Called when a full line is received over the RTT console.
 * Each character in the line is converted to a HID keycode and sent.
 */
/* Updated console_input_cb — just posts to hid_eventq, no delays here */
static void
console_input_cb(struct os_event *ev)
{
    struct console_input *inp;
    uint8_t keycode, mod;
    int i;

    if (!ev || !ev->ev_arg) {
        return;
    }

    inp = (struct console_input *)ev->ev_arg;

    MODLOG_DFLT(INFO, "Console input: '%s'\n", inp->line);

    for (i = 0; inp->line[i] != '\0'; i++) {
        if (ascii_to_hid(inp->line[i], &keycode, &mod) == 0) {
            struct hid_keyboard_input key = {
                .modifiers = mod,
                .keycodes = { keycode }
            };
            hid_send_key(&key);
            os_time_delay(os_time_ms_to_ticks32(20));
        } else {
            MODLOG_DFLT(INFO, "Unmapped char: 0x%02x\n",
                        (unsigned char)inp->line[i]);
        }
    }

    /* Return the event to the console so it can be reused */
    console_line_event_put(ev);
}

/* ISR: parse one incoming byte into the TLV state machine.
 * Copies the completed frame to tlv_pending and posts an event. */
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

/* Queue a short response frame and trigger TX. */
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

/* Task context: dispatch the completed TLV frame. */
static void
uart_dispatch(const struct tlv_frame *f)
{
    uint8_t keycode, mod;
    int i;

    switch (f->type) {
    case CMD_SEND_KEY:
        if (f->length >= 1) {
            struct hid_keyboard_input key = {
                .modifiers = (f->length >= 2) ? f->value[1] : 0,
                .keycodes  = { f->value[0] }
            };
            hid_send_key(&key);
        }
        break;

    case CMD_SEND_STRING:
        for (i = 0; i < f->length; i++) {
            if (ascii_to_hid((char)f->value[i], &keycode, &mod) == 0) {
                struct hid_keyboard_input key = {
                    .modifiers = mod,
                    .keycodes  = { keycode }
                };
                hid_send_key(&key);
                os_time_delay(os_time_ms_to_ticks32(20));
            }
        }
        break;

    case CMD_CLEAR_BONDS:
        ble_store_clear();
        MODLOG_DFLT(INFO, "Bonds cleared\n");
        break;

    case CMD_BLE_CTRL:
        if (f->length >= 1 && f->value[0] == 0x00) {
            /* BLE off: drop connection then stop advertising */
            if (ble_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
                ble_gap_terminate(ble_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            }
            ble_gap_adv_stop();
            MODLOG_DFLT(INFO, "BLE off\n");
        } else {
            /* BLE on: (re)start advertising */
            ble_advertise();
            MODLOG_DFLT(INFO, "BLE on\n");
        }
        break;

    case CMD_GET_TIME:
        if (ble_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
            MODLOG_DFLT(INFO, "GET_TIME: not connected\n");
        } else if (cts_chr_handle == 0) {
            MODLOG_DFLT(INFO, "GET_TIME: CTS not discovered, retrying\n");
            cts_discover(ble_conn_handle);
        } else {
            ble_gattc_read(ble_conn_handle, cts_chr_handle, cts_read_cb, NULL);
        }
        break;

    case CMD_PING: {
        static const uint8_t ack[] = { CMD_PING, 0x01, 0xAC };
        uart_tx_send(ack, sizeof(ack));
        MODLOG_DFLT(INFO, "UART ping OK\n");
        break;
    }

    default:
        MODLOG_DFLT(INFO, "UART: unknown cmd 0x%02x\n", f->type);
        break;
    }
}

static void
uart_rx_event_cb(struct os_event *ev)
{
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

    uart_dev = (struct uart_dev *)os_dev_open("uart0", OS_TIMEOUT_NEVER, &uc);
    assert(uart_dev != NULL);

    MODLOG_DFLT(INFO, "UART TLV receiver ready (115200 8N1)\n");
}

int
main(int argc, char **argv)
{
    int rc;

    /* Initialize all packages. */
    sysinit();

    MODLOG_DFLT(INFO, "BLE HID keyboard demo\n");

    /* Clear any stale bonds from previous failed pairing attempts */
    ble_store_clear();
    MODLOG_DFLT(INFO, "Cleared bond store\n");

    /* Set up console input over RTT */
    console_event.ev_cb = console_input_cb;
    console_event.ev_arg = &console_buf;
    console_line_queue_set(os_eventq_dflt_get());
    console_line_event_put(&console_event);
    MODLOG_DFLT(INFO, "Console input ready (type text + Enter in RTT)\n");

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Security Manager configuration for HID bonding */
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;        /* Just Works */
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;                             /* No MITM needed */
    ble_hs_cfg.sm_sc = 1;                               /* Secure Connections (uses HCI RNG) */
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;

    rc = hid_gatt_init();
    assert(rc == 0);

    rc = conf_load();
    assert(rc == 0);

    rc = ble_svc_gap_device_name_set(device_name);
    assert(rc == 0);

    /* Set GAP appearance to HID Keyboard (0x03C1 / 961) */
    rc = ble_svc_gap_device_appearance_set(961);
    assert(rc == 0);

    uart_init();

    /* As the last thing, process events from default event queue. */
    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }

    return 0;
}
