# SPDX-License-Identifier: Apache-2.0

board_runner_args(openocd "--use-elf")
board_runner_args(mdb-hw "--jtag=digilent" "--cores=${CONFIG_MP_MAX_NUM_CPUS}")
include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/mdb-hw.board.cmake)
