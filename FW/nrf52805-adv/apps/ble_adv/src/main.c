#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "os/os.h"
#include "sysinit/sysinit.h"
#include "log/log.h"
#include "host/ble_hs.h"
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

static const char *device_name = "Apache Mynewt";

/* adv_event() calls advertise(), so forward declaration is required */
static void ble_advertise(void);

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
adv_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_ADV_COMPLETE:
        MODLOG_DFLT(INFO, "Advertising completed, termination code: %d\n",
                    event->adv_complete.reason);
        ble_advertise();
        return 0;
    default:
        MODLOG_DFLT(ERROR, "Advertising event not handled\n");
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
    adv_params.conn_mode = BLE_GAP_CONN_MODE_NON;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    memset(&fields, 0, sizeof(fields));

    /* Fill the fields with advertising data - flags, tx power level, name */
    fields.flags = BLE_HS_ADV_F_DISC_GEN;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    assert(rc == 0);

    MODLOG_DFLT(INFO, "Starting advertising...\n");

    /* As own address type we use hard-coded value, because we generate
       NRPA and by definition it's random */
    rc = ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, 10000,
                           &adv_params, adv_event, NULL);
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

    printf("BLE advertising app\n");

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    rc = ble_svc_gap_device_name_set(device_name);
    assert(rc == 0);

    /* As the last thing, process events from default event queue. */
    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }

    return 0;
}
