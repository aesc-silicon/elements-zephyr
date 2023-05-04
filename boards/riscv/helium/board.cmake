# SPDX-License-Identifier: Apache-2.0

board_set_flasher_ifnset(openfpgaloader)
board_finalize_runner_args(openfpgaloader)

board_runner_args(openocd --no-halt --no-init --no-load)
board_runner_args(openocd --gdb-init "set remotetimeout 2000")
board_runner_args(openocd --gdb-init "set arch riscv:rv32")
board_runner_args(openocd --gdb-init "monitor reset halt")
board_runner_args(openocd --gdb-init "load")
board_runner_args(openocd --config "tcl/interface/jlink.cfg")
board_runner_args(openocd --config "${BOARD_DIR}/support/helium1.cfg")
board_runner_args(openocd --openocd "${ZEPHYR_BASE}/../openocd/src/openocd")
board_runner_args(openocd --openocd-search "${ZEPHYR_BASE}/../openocd")

include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
