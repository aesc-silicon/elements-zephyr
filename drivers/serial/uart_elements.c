/*
 * Copyright (c) 2023 aesc silicon
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT elements_uart

#include <errno.h>
#include <soc.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <elements/drivers/ip_identification.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(elements_uart, CONFIG_UART_LOG_LEVEL);

#define DEV_UART_IRQ_TX_EN		(1 << 0)
#define DEV_UART_IRQ_RX_EN		(1 << 1)

struct uart_elements_data {
	DEVICE_MMIO_NAMED_RAM(regs);
};

struct uart_elements_config {
	DEVICE_MMIO_NAMED_ROM(regs);
	uint64_t sys_clk_freq;
	uint32_t current_speed;
};

struct uart_elements_regs {
	uint32_t data_width;
	uint32_t sampling_sizes;
	uint32_t fifo_depths;
	uint32_t permissions;
	uint32_t read_write;
	uint32_t status;
	uint32_t clock_div;
	uint32_t frame_cfg;
	uint32_t ip;
	uint32_t ie;
};

#define DEV_CFG(dev)							     \
	((struct uart_elements_config *)(dev)->config)
#define DEV_DATA(dev) ((struct uart_elements_data *)(dev)->data)
#define DEV_UART(dev)							     \
	((struct uart_elements_regs *)DEVICE_MMIO_NAMED_GET(dev, regs))


static void uart_elements_poll_out(const struct device *dev, unsigned char c)
{
	volatile struct uart_elements_regs *uart = DEV_UART(dev);

	while ((uart->status & 0x00FF0000) == 0);
	uart->read_write = c;
}

static int uart_elements_poll_in(const struct device *dev, unsigned char *c)
{
	volatile struct uart_elements_regs *uart = DEV_UART(dev);
	int val;

	val = uart->read_write;
	if (val & 0x10000) {
		*c = val & 0xFF;
		return 0;
	}

	return -1;
}

static int uart_elements_init(const struct device *dev)
{
	volatile struct uart_elements_config *cfg = DEV_CFG(dev);
	volatile uintptr_t *base_addr = (volatile uintptr_t *)DEV_UART(dev);
	volatile struct uart_elements_regs *uart;

	DEVICE_MMIO_NAMED_MAP(dev, regs, K_MEM_CACHE_NONE);
	LOG_INF("IP core version: %i.%i.%i.",
		ip_id_get_major_version(base_addr),
		ip_id_get_minor_version(base_addr),
		ip_id_get_patchlevel(base_addr)
	);
	DEVICE_MMIO_NAMED_GET(dev, regs) = ip_id_relocate_driver(base_addr);
	LOG_INF("Relocate driver to address 0x%lx.", DEVICE_MMIO_NAMED_GET(dev, regs));
	uart = DEV_UART(dev);

	uart->clock_div = cfg->sys_clk_freq / cfg->current_speed / 8;
	uart->frame_cfg = 7;

	return 0;
}


static const struct uart_driver_api uart_elements_driver_api = {
	.poll_in          = uart_elements_poll_in,
	.poll_out         = uart_elements_poll_out,
	.err_check        = NULL,
};

#define ELEMENTS_UART_INIT(no)						     \
	static struct uart_elements_data uart_elements_dev_data_##no;	     \
	static struct uart_elements_config uart_elements_dev_cfg_##no = {    \
		DEVICE_MMIO_NAMED_ROM_INIT(regs,			     \
					   DT_INST(no, elements_uart)),	     \
		.sys_clk_freq =						     \
			DT_PROP(DT_INST(no, elements_uart), clock_frequency),\
		.current_speed =					     \
			DT_PROP(DT_INST(no, elements_uart), current_speed),  \
	};								     \
	DEVICE_DT_INST_DEFINE(no,					     \
			      uart_elements_init,			     \
			      NULL,					     \
			      &uart_elements_dev_data_##no,		     \
			      &uart_elements_dev_cfg_##no,		     \
			      PRE_KERNEL_1,				     \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,	     \
			      (void *)&uart_elements_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ELEMENTS_UART_INIT)
