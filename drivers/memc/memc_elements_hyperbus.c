/*
 * Copyright (c) 2024 aesc silicon
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT elements_hyperbus

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <soc.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(memc_elements_hyperbus, CONFIG_MEMC_LOG_LEVEL);


struct memc_elements_hyperbus_config {
	uint32_t regs;
};

struct memc_elements_hyperbus_regs {
	uint32_t reserved[4];
	uint32_t reset;
	uint32_t reset_pulse;
	uint32_t reset_hold;
	uint32_t reserved1;
	uint32_t timings;
	uint32_t reserved2[3];
	uint32_t device_registers;
	uint32_t device_registers_queues;
};

#define DEV_HYPERBUS_CFG(dev)						     \
	((struct memc_elements_hyperbus_config *)(dev)->config)
#define DEV_HYPERBUS(dev)						     \
	((struct memc_elements_hyperbus_regs *)(DEV_HYPERBUS_CFG(dev))->regs)

#define ELEMENTS_HYPERBUS_INIT(no)					     \
	static struct memc_elements_hyperbus_config			     \
		memc_elements_hyperbus_dev_cfg_##no = {			     \
		.regs = DT_REG_ADDR(DT_INST(no, DT_DRV_COMPAT)),	     \
	};								     \
	DEVICE_DT_INST_DEFINE(no,					     \
			      memc_elements_hyperbus_init,		     \
			      NULL,					     \
			      NULL,					     \
			      &memc_elements_hyperbus_dev_cfg_##no,	     \
			      PRE_KERNEL_1,				     \
			      CONFIG_MEMC_INIT_PRIORITY,		     \
			      NULL);

static int read_register(const struct device *dev, uint16_t address)
{
	volatile struct memc_elements_hyperbus_regs *hyperbus =
		DEV_HYPERBUS(dev);
	uint32_t result;

	hyperbus->device_registers = 0x00008000 | (address & 0x7FFF);
	while ((hyperbus->device_registers_queues & 0xFFFF) == 0) {}
	do {
		result = hyperbus->device_registers;
	} while (!(result >> 31));

	return result & 0xFFFF;
}

static void write_register(const struct device *dev, uint16_t address,
			   uint16_t value)
{
	volatile struct memc_elements_hyperbus_regs *hyperbus =
		DEV_HYPERBUS(dev);
	uint32_t result;

	hyperbus->device_registers = 0x0 | (address & 0x7FFF) | (value << 16);
	while ((hyperbus->device_registers_queues & 0xFFFF) == 0) {}
	do {
		result = hyperbus->device_registers;
	} while (!(result >> 31));
}

static void dump_register(const struct device *dev, uint16_t address,
			 char *name)
{
	uint32_t result = read_register(dev, address);

	LOG_INF("%s: 0x%04x", name, result);
}

static void set_latency_timings(const struct device *dev, uint8_t latency)
{
	volatile struct memc_elements_hyperbus_regs *hyperbus =
		DEV_HYPERBUS(dev);

	switch (latency & 0xf) {
		case 0xe:
			LOG_DBG("Set latency cycles to 3");
			hyperbus->timings = 3;
			break;
		case 0xf:
			LOG_DBG("Set latency cycles to 4");
			hyperbus->timings = 4;
			break;
		case 0:
			LOG_DBG("Set latency cycles to 5");
			hyperbus->timings = 5;
			break;
		case 1:
			LOG_DBG("Set latency cycles to 6");
			hyperbus->timings = 6;
			break;
		case 2:
			LOG_DBG("Set latency cycles to 7");
			hyperbus->timings = 7;
			break;
		default:
			LOG_DBG("Unknown number of clock latency");
			hyperbus->timings = 7;
	}
}

static void set_latency_clocks(const struct device *dev, uint8_t clocks)
{
	uint32_t value, reg;

	switch (clocks) {
		case 3:
			value = 0x3;
			break;
		case 4:
			value = 0xf;
			break;
		case 5:
			value = 0;
			break;
		case 6:
			value = 1;
			break;
		case 7:
			value = 2;
			break;
		default:
			value = 2;
			break;
	}

	reg = read_register(dev, 0x800);
	reg &= ~0x70;
	reg |= (value & 0xf) << 4;
	write_register(dev, 0x800, reg);
	set_latency_timings(dev, value & 0xf);
}

static int memc_elements_hyperbus_init(const struct device *dev)
{
	volatile struct memc_elements_hyperbus_regs *hyperbus =
		DEV_HYPERBUS(dev);
	uint32_t reg;

	hyperbus->reset_pulse = 0x20;
	hyperbus->reset_hold = 0x40;
	hyperbus->reset = 0x1;

	hyperbus->timings = 0;

	reg = read_register(dev, 0x800);
	set_latency_timings(dev, (reg >> 4) & 0xf);

	if (DT_PROP(DT_INST(0, DT_DRV_COMPAT), variable_latency)) {
		LOG_DBG("Fixed Latency Disabled");
		reg = read_register(dev, 0x800);
		reg &= ~(1 << 3);
		write_register(dev, 0x800, reg);
	}

#if DT_NODE_HAS_PROP(DT_INST(0, DT_DRV_COMPAT), latency_cycles)
	set_latency_clocks(dev, DT_PROP(DT_INST(0, DT_DRV_COMPAT), latency_cycles));
#endif

	dump_register(dev, 0x0, "Identification Register 0");
	dump_register(dev, 0x1, "Identification Register 1");
	dump_register(dev, 0x800, "Configuration Register 0");
	dump_register(dev, 0x801, "Configuration Register 1");

	return 0;
}

ELEMENTS_HYPERBUS_INIT(0)
