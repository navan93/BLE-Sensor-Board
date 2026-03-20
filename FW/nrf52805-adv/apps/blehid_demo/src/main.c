#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "os/os.h"
#include "sysinit/sysinit.h"
#include "log/log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_mbuf.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

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

static const char *device_name = "Sensor Watch F91-W";

/* HID keyboard state */
static uint16_t hid_input_report_handle;
static uint8_t hid_input_report[8];
static uint16_t ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static int hid_notify_enabled;

/* Forward declarations */
static void ble_advertise(void);
static int gap_event(struct ble_gap_event *event, void *arg);
static int hid_gatt_init(void);
static void hid_send_test_key(void);
static int hid_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg);

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
    0x05, 0x07,       /*   Usage Page (Key Codes) */
    0x19, 0xE0,       /*   Usage Minimum (224) */
    0x29, 0xE7,       /*   Usage Maximum (231) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x25, 0x01,       /*   Logical Maximum (1) */
    0x75, 0x01,       /*   Report Size (1) */
    0x95, 0x08,       /*   Report Count (8) – modifier bits */
    0x81, 0x02,       /*   Input (Data,Var,Abs) */
    0x95, 0x01,       /*   Report Count (1) */
    0x75, 0x08,       /*   Report Size (8) */
    0x81, 0x01,       /*   Input (Const,Array,Abs) – reserved */
    0x95, 0x05,       /*   Report Count (5) */
    0x75, 0x01,       /*   Report Size (1) */
    0x05, 0x08,       /*   Usage Page (LEDs) */
    0x19, 0x01,       /*   Usage Minimum (1) */
    0x29, 0x05,       /*   Usage Maximum (5) */
    0x91, 0x02,       /*   Output (Data,Var,Abs) – LED report */
    0x95, 0x01,       /*   Report Count (1) */
    0x75, 0x03,       /*   Report Size (3) */
    0x91, 0x01,       /*   Output (Const,Array,Abs) – LED padding */
    0x95, 0x06,       /*   Report Count (6) */
    0x75, 0x08,       /*   Report Size (8) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x25, 0x65,       /*   Logical Maximum (101) */
    0x05, 0x07,       /*   Usage Page (Key Codes) */
    0x19, 0x00,       /*   Usage Minimum (0) */
    0x29, 0x65,       /*   Usage Maximum (101) */
    0x81, 0x00,       /*   Input (Data,Array) – 6 keycodes */
    0xC0              /* End Collection */
};

/* HID service definition */
static const struct ble_gatt_svc_def hid_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812), /* HID Service */
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4A), /* HID Information */
                .access_cb = hid_access_cb,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4B), /* Report Map */
                .access_cb = hid_access_cb,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4C), /* HID Control Point */
                .access_cb = hid_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4D), /* Input Report */
                .access_cb = hid_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &hid_input_report_handle,
            },
            {
                0, /* No more characteristics */
            }
        },
    },
    {
        0, /* No more services */
    }
};

static void
set_ble_addr(void)
{
    int rc;
    ble_addr_t addr;

    /* generate new non-resolvable private address */
    rc = ble_hs_id_gen_rnd(1, &addr);
    assert(rc == 0);

    /* set generated address */
    rc = ble_hs_id_set_rnd(addr.val);
    assert(rc == 0);
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
        case 0x2A4B: /* Report Map */
            rc = os_mbuf_append(ctxt->om, hid_report_map,
                                sizeof(hid_report_map));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        case 0x2A4D: /* Input Report */
            rc = os_mbuf_append(ctxt->om, hid_input_report,
                                sizeof(hid_input_report));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        default:
            return BLE_ATT_ERR_UNLIKELY;
        }

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        uuid16 = ble_uuid_u16(ctxt->chr->uuid);
        if (uuid16 == 0x2A4C) {
            /* HID Control Point – ignore for demo */
            return 0;
        }
        return BLE_ATT_ERR_UNLIKELY;

    default:
        return BLE_ATT_ERR_UNLIKELY;
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

    return 0;
}

static void
hid_send_test_key(void)
{
    int rc;
    struct os_mbuf *om;

    if (ble_conn_handle == BLE_HS_CONN_HANDLE_NONE || !hid_notify_enabled) {
        return;
    }

    /* Press 'a' (HID keycode 0x04) */
    memset(hid_input_report, 0, sizeof(hid_input_report));
    hid_input_report[2] = 0x04;

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

    /* Then release all keys */
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
        } else {
            ble_advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        MODLOG_DFLT(INFO, "Disconnect; reason=%d\n",
                    event->disconnect.reason);
        ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        hid_notify_enabled = 0;
        ble_advertise();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == hid_input_report_handle) {
            hid_notify_enabled = event->subscribe.cur_notify;
            MODLOG_DFLT(INFO, "HID input notify=%d\n", hid_notify_enabled);
            if (hid_notify_enabled) {
                hid_send_test_key();
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

static void
ble_advertise(void)
{
    int rc;
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;

    /* set adv parameters */
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min  = 2400; // 500ms / 0.625ms = 800
    adv_params.itvl_max  = 4800;

    memset(&fields, 0, sizeof(fields));

    /* Fill the fields with advertising data - flags, tx power level, name */
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;

    /* Advertise the HID service UUID (0x1812) so centrals can filter for HID */
    fields.uuids16 = (ble_uuid16_t[]){ BLE_UUID16_INIT(0x1812) };
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    assert(rc == 0);

    MODLOG_DFLT(INFO, "Starting advertising...\n");

    /* As own address type we use hard-coded value, because we generate
       NRPA and by definition it's random */
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

int
main(int argc, char **argv)
{
    int rc;

    /* Initialize all packages. */
    sysinit();

    printf("BLE HID keyboard demo\n");

    rc = hid_gatt_init();
    assert(rc == 0);

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    rc = ble_svc_gap_device_name_set(device_name);
    assert(rc == 0);

    /* Set GAP appearance to HID Keyboard (0x03C1 / 961) */
    rc = ble_svc_gap_device_appearance_set(961);
    assert(rc == 0);

    /* As the last thing, process events from default event queue. */
    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }

    return 0;
}
