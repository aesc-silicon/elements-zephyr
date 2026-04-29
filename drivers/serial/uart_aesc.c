/*
 * Copyright (c) 2025 Aesc Silicon
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT aesc_uart

#include <errno.h>
#include <ip_identification.h>
#include <soc.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(aesc_uart, CONFIG_UART_LOG_LEVEL);

struct uart_aesc_data {
	DEVICE_MMIO_RAM;
	uintptr_t reg_base;
};

struct uart_aesc_config {
	DEVICE_MMIO_ROM;
	uint64_t sys_clk_freq;
	uint32_t current_speed;
	const struct pinctrl_dev_config *pcfg;
};

struct uart_aesc_regs {
	uint32_t data_width;
	uint32_t sampling_sizes;
	uint32_t fifo_depths;
	uint32_t permissions;
	uint32_t read_write;
	uint32_t fifo_status;
	uint32_t clock_div;
	uint32_t frame_cfg;
	uint32_t ip;
	uint32_t ie;
};

#define DEV_CFG(dev) ((struct uart_aesc_config *)(dev)->config)
#define DEV_DATA(dev) ((struct uart_aesc_data *)(dev)->data)
#define DEV_UART(dev)							      \
	((volatile struct uart_aesc_regs *)DEV_DATA(dev)->reg_base)

#define AESC_UART_IRQ_TX_EN			BIT(0)
#define AESC_UART_IRQ_RX_EN			BIT(1)
#define AESC_UART_FIFO_TX_COUNT_MASK		GENMASK(23, 16)
#define AESC_UART_READ_FIFO_VALID_BIT		BIT(16)

static void uart_aesc_poll_out(const struct device *dev, unsigned char c)
{
	volatile struct uart_aesc_regs *uart = DEV_UART(dev);

	while ((uart->fifo_status & AESC_UART_FIFO_TX_COUNT_MASK) == 0) {
		/* Wait until transmit fifo is empty */
	}

	uart->read_write = c;
}

static int uart_aesc_poll_in(const struct device *dev, unsigned char *c)
{
	volatile struct uart_aesc_regs *uart = DEV_UART(dev);
	int val;

	val = uart->read_write;
	if (val & AESC_UART_READ_FIFO_VALID_BIT) {
		*c = val & 0xFF;
		return 0;
	}

	return -1;
}

static int uart_aesc_init(const struct device *dev)
{
	DEVICE_MMIO_MAP(dev, K_MEM_CACHE_NONE);
	const struct uart_aesc_config *cfg = DEV_CFG(dev);
	volatile uintptr_t *base_addr =
		(volatile uintptr_t *)DEVICE_MMIO_GET(dev);
	struct uart_aesc_data *data = DEV_DATA(dev);
	volatile struct uart_aesc_regs *uart;
	int ret;

	LOG_DBG("IP core version: %i.%i.%i.",
		ip_id_get_major_version(base_addr),
		ip_id_get_minor_version(base_addr),
		ip_id_get_patchlevel(base_addr)
	);
	data->reg_base = ip_id_relocate_driver(base_addr);
	LOG_DBG("Relocate driver to address 0x%lx.", data->reg_base);
	uart = DEV_UART(dev);

	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("failed to apply pinctrl");
		return ret;
	}

	uart->clock_div = cfg->sys_clk_freq / cfg->current_speed / 8;
	uart->frame_cfg = 7;

	return 0;
}

static DEVICE_API(uart, uart_aesc_driver_api) = {
	.poll_in          = uart_aesc_poll_in,
	.poll_out         = uart_aesc_poll_out,
	.err_check        = NULL,
};

#define AESC_UART_INIT(no)						     \
	PINCTRL_DT_INST_DEFINE(no);					     \
	static struct uart_aesc_data uart_aesc_dev_data_##no;		     \
	static struct uart_aesc_config uart_aesc_dev_cfg_##no = {	     \
		DEVICE_MMIO_ROM_INIT(DT_DRV_INST(no)),			     \
		.sys_clk_freq =						     \
			DT_PROP(DT_INST(no, aesc_uart), clock_frequency),    \
		.current_speed =					     \
			DT_PROP(DT_INST(no, aesc_uart), current_speed),	     \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(no),		     \
	};								     \
	DEVICE_DT_INST_DEFINE(no,					     \
			      uart_aesc_init,				     \
			      NULL,					     \
			      &uart_aesc_dev_data_##no,			     \
			      &uart_aesc_dev_cfg_##no,			     \
			      PRE_KERNEL_1,				     \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,	     \
			      (void *)&uart_aesc_driver_api);

DT_INST_FOREACH_STATUS_OKAY(AESC_UART_INIT)
