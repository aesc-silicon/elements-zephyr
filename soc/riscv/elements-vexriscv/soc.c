/*
 * Copyright (c) 2023 aesc silicon
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#if defined(CONFIG_REBOOT)
struct reset_elements_regs {
	uint32_t enable;
	uint32_t trigger;
	uint32_t acknowledge;
};

void sys_arch_reboot(int type)
{
	struct reset_elements_regs *reset =
		(struct reset_elements_regs *)0xf0021000;
	reset->trigger = 0x3;
	reset->acknowledge = 0xcafe;
}
#endif
