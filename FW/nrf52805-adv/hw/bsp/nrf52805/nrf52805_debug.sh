#!/bin/sh

# This script attaches a gdb session to a Mynewt image running on your BSP.

# If your BSP uses JLink, a good example script to copy is:
#     repos/apache-mynewt-core/hw/bsp/nrf52dk/nrf52dk_debug.sh
#
# If your BSP uses OpenOCD, a good example script to copy is:
#     repos/apache-mynewt-core/hw/bsp/rb-nano2/rb-nano2_debug.sh

# Called with following variables set:
#  - CORE_PATH is absolute path to @apache-mynewt-core
#  - BSP_PATH is absolute path to hw/bsp/bsp_name
#  - BIN_BASENAME is the path to prefix to target binary,
#    .elf appended to name is the ELF file
#  - FEATURES holds the target features string
#  - EXTRA_JTAG_CMD holds extra parameters to pass to jtag software
#  - RESET set if target should be reset when attaching
#  - NO_GDB set if we should not start gdb to debug
#

. $CORE_PATH/hw/scripts/openocd.sh

FILE_NAME=$BIN_BASENAME.elf

CFG="-f $BSP_PATH/nrf52swd.cfg"

# Exit openocd when gdb detaches.
EXTRA_JTAG_CMD="$EXTRA_JTAG_CMD; nrf52.cpu configure -event gdb-detach {if {[nrf52.cpu curstate] eq \"halted\"} resume;shutdown}"

# Start RTT server
# EXTRA_GDB_CMDS='monitor rtt setup 0x20000000 0x1000 "SEGGER RTT"; monitor rtt start; monitor rtt server start 5000 0; break main;'
EXTRA_GDB_CMDS="monitor rtt setup 0x20000000 0x1000 \"SEGGER RTT\"\nmonitor rtt start\nmonitor rtt server start 5000 0"
# EXTRA_GDB_CMDS="$EXTRA_GDB_CMDS\n break main\nlayout src\nc"
RESET=1
openocd_debug