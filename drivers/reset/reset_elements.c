/*
 * Copyright (c) 2025 aesc silicon
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#define DT_DRV_COMPAT elements_rst

#include <zephyr/kernel.h>
#include <zephyr/drivers/reset.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(elements_rst);

struct reset_elements_config {
	DEVICE_MMIO_NAMED_ROM(regs);
};

struct reset_elements_regs {
	uint32_t enable;
	uint32_t trigger;
	uint32_t acknowledge;
};

#define DEV_RESET(dev) 							      \
	((struct reset_elements_regs *)DEVICE_MMIO_NAMED_GET(dev, regs))

static int reset_elements_line_toggle(const struct device *dev, uint32_t id)
{
	const struct reset_elements_regs *reset = DEV_RESET(dev);
	reset->trigger = 0x3;
	reset->acknowledge = 0xcafe;
	return 0;
}

static DEVICE_API(reset, reset_elements_driver_api) = {
	.line_toggle = reset_elements_line_toggle,
};

static const struct reset_elements_config reset_elements_config = {
	DEVICE_MMIO_NAMED_ROM_INIT(regs, DT_INST_REG_ADDR(0)),
};

DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, &reset_elements_config,
		      PRE_KERNEL_1, CONFIG_RESET_INIT_PRIORITY,
		      &reset_elements_driver_api);
