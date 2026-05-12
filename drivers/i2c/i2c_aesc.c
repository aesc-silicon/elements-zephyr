/*
 * SPDX-FileCopyrightText: 2026 Aesc Silicon
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <ip_identification.h>
#include <soc.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/irq.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(aesc_i2c, CONFIG_I2C_LOG_LEVEL);

#define DT_DRV_COMPAT aesc_i2c

struct i2c_aesc_data {
	DEVICE_MMIO_RAM;
	uintptr_t reg_base;
	uint32_t dev_config;
};

struct i2c_aesc_config {
	DEVICE_MMIO_ROM;
	uint64_t sys_clk_freq;
	uint32_t i2c_freq;
	const struct pinctrl_dev_config *pcfg;
	void (*irq_config)(const struct device *dev);
};

/*
 * Register map (relative to ip_id_relocate_driver base):
 *
 *   0x00  data_width    clockDividerWidth [7:0]
 *   0x04  fifo_depths   cmdFifoDepth [15:8], rspFifoDepth [7:0]
 *   0x08  permissions   busCanWriteClockDividerConfig [0]
 *   0x0C  read_write    write: cmd [11:0]  read: valid [31], error [8], data [7:0]
 *   0x10  fifo_status   cmd vacancy [31:16], rsp occupancy [15:0]
 *   0x14  clock_div     clock divider value
 *   0x18  (reserved)
 *   0x1C  cmd_trigger   cmd FIFO occupancy interrupt threshold
 *   0x20  ip            IRQ pending (W1C)
 *   0x24  ie            IRQ enable mask
 *   0x28  error_pending error pending (W1C): [0] NACK, [1] arb lost, [2] rsp overflow
 *   0x2C  error_enable  error enable mask
 */
struct i2c_aesc_regs {
	uint32_t data_width;
	uint32_t fifo_depths;
	uint32_t permissions;
	uint32_t read_write;
	uint32_t fifo_status;
	uint32_t clock_div;
	uint32_t reserved;
	uint32_t cmd_trigger;
	uint32_t ip;
	uint32_t ie;
	uint32_t error_pending;
	uint32_t error_enable;
};

#define DEV_CFG(dev)  ((struct i2c_aesc_config *)(dev)->config)
#define DEV_DATA(dev) ((struct i2c_aesc_data *)(dev)->data)
#define DEV_I2C(dev)							     \
	((volatile struct i2c_aesc_regs *)DEV_DATA(dev)->reg_base)

/* CMD word written to read_write */
#define AESC_I2C_CMD_DATA_MASK		GENMASK(7, 0)
#define AESC_I2C_CMD_START		BIT(8)
#define AESC_I2C_CMD_STOP		BIT(9)
#define AESC_I2C_CMD_READ		BIT(10)
#define AESC_I2C_CMD_ACK		BIT(11)

/* RSP word read from read_write */
#define AESC_I2C_RSP_VALID		BIT(31)
#define AESC_I2C_RSP_ERROR		BIT(8)
#define AESC_I2C_RSP_DATA_MASK		GENMASK(7, 0)

/* fifo_status */
#define AESC_I2C_CMD_VACANCY_MASK	GENMASK(31, 16)
#define AESC_I2C_CMD_VACANCY_SHIFT	16
#define AESC_I2C_RSP_OCCUPANCY_MASK	GENMASK(15, 0)

static void i2c_aesc_write_cmd(volatile struct i2c_aesc_regs *i2c, uint32_t cmd)
{
	while (!((i2c->fifo_status & AESC_I2C_CMD_VACANCY_MASK)
		 >> AESC_I2C_CMD_VACANCY_SHIFT)) {
		/* wait for space in CMD FIFO */
	}
	i2c->read_write = cmd;
}

static int i2c_aesc_read_rsp(volatile struct i2c_aesc_regs *i2c,
			      uint8_t *data, bool *error)
{
	uint32_t val;

	while (!(i2c->fifo_status & AESC_I2C_RSP_OCCUPANCY_MASK)) {
		/* wait for response */
	}
	val = i2c->read_write;
	if (!(val & AESC_I2C_RSP_VALID)) {
		return -EIO;
	}
	if (error) {
		*error = !!(val & AESC_I2C_RSP_ERROR);
	}
	if (data) {
		*data = val & AESC_I2C_RSP_DATA_MASK;
	}
	return 0;
}

static int i2c_aesc_configure(const struct device *dev, uint32_t dev_config)
{
	const struct i2c_aesc_config *cfg = DEV_CFG(dev);
	struct i2c_aesc_data *data = DEV_DATA(dev);
	volatile struct i2c_aesc_regs *i2c = DEV_I2C(dev);
	uint32_t scl_freq;

	if (!(dev_config & I2C_MODE_CONTROLLER)) {
		return -ENOTSUP;
	}

	switch (I2C_SPEED_GET(dev_config)) {
	case I2C_SPEED_STANDARD:
		scl_freq = 100000U;
		break;
	case I2C_SPEED_FAST:
		scl_freq = 400000U;
		break;
	case I2C_SPEED_FAST_PLUS:
		scl_freq = 1000000U;
		break;
	default:
		return -ENOTSUP;
	}

	/* Each SCL bit takes 4 clock-divider ticks: divider = sys_clk / (4 * scl) - 1 */
	i2c->clock_div = (uint32_t)(cfg->sys_clk_freq / (4U * scl_freq)) - 1U;
	data->dev_config = dev_config;

	return 0;
}

static int i2c_aesc_get_config(const struct device *dev, uint32_t *dev_config)
{
	*dev_config = DEV_DATA(dev)->dev_config;
	return 0;
}

static int i2c_aesc_transfer(const struct device *dev,
			      struct i2c_msg *msgs,
			      uint8_t num_msgs,
			      uint16_t addr)
{
	volatile struct i2c_aesc_regs *i2c = DEV_I2C(dev);
	int ret;

	for (uint8_t i = 0; i < num_msgs; i++) {
		struct i2c_msg *msg = &msgs[i];
		bool is_read  = !!(msg->flags & I2C_MSG_READ);
		bool do_stop  = !!(msg->flags & I2C_MSG_STOP);
		bool do_start = (i == 0) || !!(msg->flags & I2C_MSG_RESTART);
		bool error;

		if (msg->flags & I2C_MSG_ADDR_10_BITS) {
			return -ENOTSUP;
		}

		if (do_start) {
			/* START + 7-bit address + R/W bit */
			uint32_t cmd = AESC_I2C_CMD_START |
				((uint32_t)(addr << 1 | (is_read ? 1 : 0))
				 & AESC_I2C_CMD_DATA_MASK);

			if (msg->len == 0 && do_stop) {
				cmd |= AESC_I2C_CMD_STOP;
			}

			i2c_aesc_write_cmd(i2c, cmd);
			ret = i2c_aesc_read_rsp(i2c, NULL, &error);
			if (ret < 0 || error) {
				return -EIO;
			}

			if (msg->len == 0) {
				continue;
			}
		}

		for (uint32_t j = 0; j < msg->len; j++) {
			bool last_byte = (j == msg->len - 1U);
			bool byte_stop = do_stop && last_byte;
			uint32_t cmd;

			if (is_read) {
				/* ACK all bytes except the last; NACK the last to
				 * signal end of read to the device. */
				cmd = AESC_I2C_CMD_READ |
				      (last_byte ? 0U : AESC_I2C_CMD_ACK) |
				      (byte_stop  ? AESC_I2C_CMD_STOP : 0U);
				i2c_aesc_write_cmd(i2c, cmd);
				ret = i2c_aesc_read_rsp(i2c, &msg->buf[j], NULL);
				if (ret < 0) {
					return ret;
				}
			} else {
				cmd = (msg->buf[j] & AESC_I2C_CMD_DATA_MASK) |
				      (byte_stop ? AESC_I2C_CMD_STOP : 0U);
				i2c_aesc_write_cmd(i2c, cmd);
				ret = i2c_aesc_read_rsp(i2c, NULL, &error);
				if (ret < 0 || error) {
					return -EIO;
				}
			}
		}
	}

	return 0;
}

static void i2c_aesc_isr(const struct device *dev)
{
	volatile struct i2c_aesc_regs *i2c = DEV_I2C(dev);

	/* Clear all pending interrupts (W1C) */
	i2c->ip = i2c->ip;
	i2c->error_pending = i2c->error_pending;
}

static int i2c_aesc_init(const struct device *dev)
{
	DEVICE_MMIO_MAP(dev, K_MEM_CACHE_NONE);
	const struct i2c_aesc_config *cfg = DEV_CFG(dev);
	volatile uintptr_t *base_addr =
		(volatile uintptr_t *)DEVICE_MMIO_GET(dev);
	struct i2c_aesc_data *data = DEV_DATA(dev);
	volatile struct i2c_aesc_regs *i2c;
	uint32_t speed;
	int ret;

	LOG_DBG("IP core version: %i.%i.%i.",
		ip_id_get_major_version(base_addr),
		ip_id_get_minor_version(base_addr),
		ip_id_get_patchlevel(base_addr)
	);
	data->reg_base = ip_id_relocate_driver(base_addr);
	LOG_DBG("Relocate driver to address 0x%lx.", data->reg_base);
	i2c = DEV_I2C(dev);

	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("failed to apply pinctrl");
		return ret;
	}

	i2c->ie = 0;
	i2c->error_enable = 0;

	cfg->irq_config(dev);

	if (cfg->i2c_freq <= 100000U) {
		speed = I2C_SPEED_STANDARD;
	} else if (cfg->i2c_freq <= 400000U) {
		speed = I2C_SPEED_FAST;
	} else {
		speed = I2C_SPEED_FAST_PLUS;
	}

	return i2c_aesc_configure(dev, I2C_MODE_CONTROLLER | I2C_SPEED_SET(speed));
}

static DEVICE_API(i2c, i2c_aesc_driver_api) = {
	.configure  = i2c_aesc_configure,
	.get_config = i2c_aesc_get_config,
	.transfer   = i2c_aesc_transfer,
};

#define AESC_I2C_INIT(no)						     \
	PINCTRL_DT_INST_DEFINE(no);					     \
	static void i2c_aesc_irq_config_##no(const struct device *dev)	     \
	{								     \
		IRQ_CONNECT(DT_INST_IRQN(no),				     \
			    DT_INST_IRQ(no, priority),			     \
			    i2c_aesc_isr,				     \
			    DEVICE_DT_INST_GET(no),			     \
			    0);						     \
		irq_enable(DT_INST_IRQN(no));				     \
	}								     \
	static struct i2c_aesc_data i2c_aesc_dev_data_##no;		     \
	static struct i2c_aesc_config i2c_aesc_dev_cfg_##no = {		     \
		DEVICE_MMIO_ROM_INIT(DT_DRV_INST(no)),			     \
		.sys_clk_freq =						     \
			DT_PROP(DT_INST(no, aesc_i2c), input_frequency),    \
		.i2c_freq =						     \
			DT_PROP(DT_INST(no, aesc_i2c), clock_frequency),    \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(no),		     \
		.irq_config = i2c_aesc_irq_config_##no,		     \
	};								     \
	DEVICE_DT_INST_DEFINE(no,					     \
			      i2c_aesc_init,				     \
			      NULL,					     \
			      &i2c_aesc_dev_data_##no,			     \
			      &i2c_aesc_dev_cfg_##no,			     \
			      POST_KERNEL,				     \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,	     \
			      &i2c_aesc_driver_api);

DT_INST_FOREACH_STATUS_OKAY(AESC_I2C_INIT)
