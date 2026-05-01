/*
 * SPDX-FileCopyrightText: 2026 Aesc Silicon
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <ip_identification.h>
#include <soc.h>

#include <zephyr/device.h>
#include <zephyr/drivers/misc/pio_aesc/pio_aesc.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/irq.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(aesc_pio, CONFIG_PIO_AESC_LOG_LEVEL);

#define DT_DRV_COMPAT aesc_pio

struct pio_aesc_data {
	DEVICE_MMIO_RAM;
	uintptr_t reg_base;
	pio_aesc_callback_t irq_cb;
	void *irq_cb_data;
};

struct pio_aesc_config {
	DEVICE_MMIO_ROM;
	uint64_t sys_clk_freq;
	uint64_t tick_freq;
	uint32_t read_delay;
	const struct pinctrl_dev_config *pcfg;
	void (*irq_config)(const struct device *dev);
};

struct pio_aesc_regs {
	uint32_t data_width;
	uint32_t fifo_depth;
	uint32_t permissions;
	uint32_t control;
	uint32_t read_write;
	uint32_t fifo_status;
	uint32_t clock_div;
	uint32_t read_delay;
	uint32_t error_pending;
	uint32_t error_enable;
	uint32_t ip;
	uint32_t ie;
};

#define DEV_CFG(dev) ((struct pio_aesc_config *)(dev)->config)
#define DEV_DATA(dev) ((struct pio_aesc_data *)(dev)->data)
#define DEV_PIO(dev)							     \
	((volatile struct pio_aesc_regs *)DEV_DATA(dev)->reg_base)

#define AESC_PIO_CONTROL_ENABLE			BIT(0)
#define AESC_PIO_CONTROL_STOP_AT_LOOP		BIT(1)
#define AESC_PIO_EXEC_POINTER_MASK		GENMASK(7, 0)
#define AESC_PIO_WRITE_POINTER_MASK		GENMASK(15, 8)
#define AESC_PIO_WRITE_POINTER_SHIFT		8
#define AESC_PIO_CMD_FIFO_DEPTH_MASK		GENMASK(7, 0)
#define AESC_PIO_READ_FIFO_VALID_BIT		BIT(16)
#define AESC_PIO_RX_OCCUPANCY_SHIFT		24
#define AESC_PIO_IRQ_RX_EN			BIT(0)
#define AESC_PIO_IRQ_LOOP_DONE			BIT(1)

/* Lifecycle */
void pio_aesc_enable(const struct device *dev)
{
	volatile struct pio_aesc_regs *pio = DEV_PIO(dev);

	pio->control |= AESC_PIO_CONTROL_ENABLE;
}

void pio_aesc_disable(const struct device *dev)
{
	volatile struct pio_aesc_regs *pio = DEV_PIO(dev);

	pio->control &= ~AESC_PIO_CONTROL_ENABLE;
}

void pio_aesc_flush(const struct device *dev)
{
	volatile struct pio_aesc_regs *pio = DEV_PIO(dev);
	uint32_t status;
	uint32_t write_ptr;
	uint32_t exec_ptr;

	pio->control |= AESC_PIO_CONTROL_STOP_AT_LOOP;

	do {
		status = pio->fifo_status;
		exec_ptr = status & AESC_PIO_EXEC_POINTER_MASK;
		write_ptr = (status & AESC_PIO_WRITE_POINTER_MASK) >>
				AESC_PIO_WRITE_POINTER_SHIFT;
	} while (exec_ptr != write_ptr);

	pio->control &= ~AESC_PIO_CONTROL_STOP_AT_LOOP;
}

void pio_aesc_reset(const struct device *dev)
{
	volatile struct pio_aesc_regs *pio = DEV_PIO(dev);

	pio_aesc_disable(dev);
	pio->fifo_status = 0x0;
}

/* Program loading */
int pio_aesc_write_command(const struct device *dev, const struct pio_cmd *cmd)
{
	volatile struct pio_aesc_regs *pio = DEV_PIO(dev);
	uint32_t cmd_fifo_depth;
	uint32_t write_pointer;

	cmd_fifo_depth = pio->fifo_depth & AESC_PIO_CMD_FIFO_DEPTH_MASK;
	write_pointer = (pio->fifo_status & AESC_PIO_WRITE_POINTER_MASK) >>
				AESC_PIO_WRITE_POINTER_SHIFT;

	if (write_pointer == (cmd_fifo_depth - 1)) {
		return -ENOBUFS;
	}

	pio->read_write = pio_encode_cmd(cmd);
	return 0;
}

int pio_aesc_write_program(const struct device *dev, const struct pio_cmd *cmds, size_t count)
{
	int ret;

	for (uint32_t i = 0; i < count; i++) {
		ret = pio_aesc_write_command(dev, &cmds[i]);
		if (ret < 0) {
			LOG_ERR("failed to write command");
			return ret;
		}
	}

	return 0;
}

/* Configuration */
int pio_aesc_set_tick_frequency(const struct device *dev, uint32_t hz)
{
	const struct pio_aesc_config *cfg = DEV_CFG(dev);
	volatile struct pio_aesc_regs *pio = DEV_PIO(dev);

	pio->clock_div = (cfg->sys_clk_freq / hz) - 1;

	return 0;
}

int pio_aesc_set_read_delay(const struct device *dev, uint8_t ticks)
{
	volatile struct pio_aesc_regs *pio = DEV_PIO(dev);

	pio->read_delay = ticks;

	return 0;
}

/* RX results */
int pio_aesc_rx_available(const struct device *dev)
{
	volatile struct pio_aesc_regs *pio = DEV_PIO(dev);

	return (int)(pio->fifo_status >> AESC_PIO_RX_OCCUPANCY_SHIFT);
}

int pio_aesc_read(const struct device *dev, uint8_t *result)
{
	volatile struct pio_aesc_regs *pio = DEV_PIO(dev);
	int val;

	val = pio->read_write;
	if (val & AESC_PIO_READ_FIFO_VALID_BIT) {
		*result = val & 0xFF;
		return 0;
	}

	return -ENODATA;
}



/* Interrupt control */
void pio_aesc_irq_enable(const struct device *dev, uint32_t mask)
{
	volatile struct pio_aesc_regs *pio = DEV_PIO(dev);

	pio->ie |= mask;
}

void pio_aesc_irq_disable(const struct device *dev, uint32_t mask)
{
	volatile struct pio_aesc_regs *pio = DEV_PIO(dev);

	pio->ie &= ~mask;
}

void pio_aesc_irq_callback_set(const struct device *dev,
				pio_aesc_callback_t cb, void *user_data)
{
	struct pio_aesc_data *data = DEV_DATA(dev);

	data->irq_cb = cb;
	data->irq_cb_data = user_data;
}

static void pio_aesc_isr(const struct device *dev)
{
	volatile struct pio_aesc_regs *pio = DEV_PIO(dev);
	struct pio_aesc_data *data = DEV_DATA(dev);
	uint32_t pending;

	/* Read active pending interrupts, then clear them (W1C) */
	pending = pio->ip;
	pio->ip = pending;
	pio->error_pending = pio->error_pending;

	if (data->irq_cb) {
		data->irq_cb(dev, pending, data->irq_cb_data);
	}
}

static int pio_aesc_init(const struct device *dev)
{
	DEVICE_MMIO_MAP(dev, K_MEM_CACHE_NONE);
	const struct pio_aesc_config *cfg = DEV_CFG(dev);
	volatile uintptr_t *base_addr =
		(volatile uintptr_t *)DEVICE_MMIO_GET(dev);
	struct pio_aesc_data *data = DEV_DATA(dev);
	volatile struct pio_aesc_regs *pio;
	int ret;

	LOG_DBG("IP core version: %i.%i.%i.",
		ip_id_get_major_version(base_addr),
		ip_id_get_minor_version(base_addr),
		ip_id_get_patchlevel(base_addr)
	);
	data->reg_base = ip_id_relocate_driver(base_addr);
	LOG_DBG("Relocate driver to address 0x%lx.", data->reg_base);
	pio = DEV_PIO(dev);

	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("failed to apply pinctrl");
		return ret;
	}

	pio_aesc_set_tick_frequency(dev, cfg->tick_freq);
	pio_aesc_set_read_delay(dev, cfg->read_delay);
	pio->ie = 0;
	pio->error_enable = 0;

	cfg->irq_config(dev);

	return 0;
}

#define AESC_PIO_INIT(no)						     \
	PINCTRL_DT_INST_DEFINE(no);					     \
	static void pio_aesc_irq_config_##no(const struct device *dev)	     \
	{								     \
		IRQ_CONNECT(DT_INST_IRQN(no),				     \
			    DT_INST_IRQ(no, priority),			     \
			    pio_aesc_isr,				     \
			    DEVICE_DT_INST_GET(no),			     \
			    0);						     \
		irq_enable(DT_INST_IRQN(no));				     \
	}								     \
	static struct pio_aesc_data pio_aesc_dev_data_##no;		     \
	static struct pio_aesc_config pio_aesc_dev_cfg_##no = {		     \
		DEVICE_MMIO_ROM_INIT(DT_DRV_INST(no)),			     \
		.sys_clk_freq =						     \
			DT_PROP(DT_INST(no, aesc_pio), clock_frequency),     \
		.tick_freq =						     \
			DT_PROP(DT_INST(no, aesc_pio), aesc_tick_frequency), \
		.read_delay =						     \
			DT_PROP(DT_INST(no, aesc_pio), aesc_read_delay),     \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(no),		     \
		.irq_config = pio_aesc_irq_config_##no,		     \
	};								     \
	DEVICE_DT_INST_DEFINE(no,					     \
			      pio_aesc_init,				     \
			      NULL,					     \
			      &pio_aesc_dev_data_##no,			     \
			      &pio_aesc_dev_cfg_##no,			     \
			      POST_KERNEL,				     \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,	     \
			      NULL);

DT_INST_FOREACH_STATUS_OKAY(AESC_PIO_INIT)
