/*
 * Copyright (c) 2020 Phytec Messtechnik GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(i2c_hydrogen);

#include <kernel.h>
#include <device.h>
#include <drivers/i2c.h>

#include "i2c-priv.h"

struct i2c_hydrogen_data {
	unsigned int res;
};

struct i2c_hydrogen_config {
	uint32_t regs;
	uint32_t sys_clk_freq;
	uint32_t bus_clk_freq;
	uint32_t fifo_size;
};

struct i2c_hydrogen_regs {
	unsigned int read_write;
	unsigned int status;
	unsigned int config;
	unsigned int clock_div;
	unsigned int ip;
	unsigned int ie;
};

#define HYDROGEN_READ			1
#define HYDROGEN_WRITE			0

#define HYDROGEN_CMD_START		0x0100
#define HYDROGEN_CMD_STOP		0x0200
#define HYDROGEN_CMD_READ		0x0400
#define HYDROGEN_CMD_WRITE		0x0000
#define HYDROGEN_CMD_ACK		0x0800
#define HYDROGEN_CMD_NACK		0x0000

#define HYDROGEN_ADDR_READ(addr)	(addr | 0x0001)
#define HYDROGEN_ADDR_WRITE(addr)	(addr & ~0x1)

#define HYDROGEN_RSP_ERROR		0x0100

#define DEV_I2C_CFG(dev)						     \
	((struct i2c_hydrogen_config *)(dev)->config)
#define DEV_I2C(dev)							     \
	((struct i2c_hydrogen_regs *)(DEV_I2C_CFG(dev))->regs)
#define DEV_I2C_DATA(dev)						     \
	((struct i2c_hydrogen_data *)(dev)->data)

#define I2C(no)								     \
	static struct i2c_hydrogen_data i2c_hydrogen_dev_data_##no;	     \
	static struct i2c_hydrogen_config i2c_hydrogen_dev_cfg_##no = {	     \
		.regs = DT_REG_ADDR(DT_INST(no, hydrogen_i2c)),		     \
		.sys_clk_freq =						     \
			DT_PROP(DT_INST(no, hydrogen_i2c), input_frequency), \
		.bus_clk_freq =						     \
			DT_PROP(DT_INST(no, hydrogen_i2c), clock_frequency), \
	};								     \
	DEVICE_AND_API_INIT(i2c_hydrogen_##no,				     \
			    DT_PROP(DT_INST(no, hydrogen_i2c), label),	     \
			    i2c_hydrogen_init,				     \
			    &i2c_hydrogen_dev_data_##no,		     \
			    &i2c_hydrogen_dev_cfg_##no,			     \
			    PRE_KERNEL_1,				     \
			    CONFIG_KERNEL_INIT_PRIORITY_DEVICE,		     \
			    (void *)&i2c_hydrogen_driver_api);


/* Wait for a previous transfer to complete */
static inline void i2c_hydrogen_busy(const struct device *dev)
{
	volatile struct i2c_hydrogen_regs *i2c = DEV_I2C(dev);
	volatile struct i2c_hydrogen_config *cfg = DEV_I2C_CFG(dev);

	while (((i2c->status >> 16) != cfg->fifo_size)) {}
}

/* Wait until the response from the transfer is back */
static inline void i2c_hydrogen_wait(const struct device *dev)
{
	volatile struct i2c_hydrogen_regs *i2c = DEV_I2C(dev);

	while (!(i2c->status & 0xFF)) {}
}


static int i2c_hydrogen_send_addr(const struct device *dev,
				  bool read,
				  uint16_t addr)
{
	volatile struct i2c_hydrogen_regs *i2c = DEV_I2C(dev);
	uint32_t command = HYDROGEN_CMD_START | HYDROGEN_CMD_WRITE;

	i2c_hydrogen_busy(dev);

	if (read)
		i2c->read_write = command | HYDROGEN_ADDR_READ(addr);
	else
		i2c->read_write = command | HYDROGEN_ADDR_WRITE(addr);

	i2c_hydrogen_wait(dev);

	if (i2c->read_write & HYDROGEN_RSP_ERROR) {
		LOG_ERR("I2C Rx failed to acknowledge\n");
		return -EIO;
	}

	return 0;
}

static int i2c_hydrogen_write_msg(const struct device *dev,
				  struct i2c_msg *msg,
				  uint16_t addr)
{
	volatile struct i2c_hydrogen_regs *i2c = DEV_I2C(dev);
	uint32_t command;
	int rc = 0;

	rc = i2c_hydrogen_send_addr(dev, HYDROGEN_WRITE, addr);
	if (rc != 0) {
		LOG_ERR("I2C failed to write message\n");
		return rc;
	}

	for (int i = 0; i < msg->len; i++) {
		command = 0 | HYDROGEN_CMD_WRITE;
		/* On the last byte of the message */
		if (i == (msg->len - 1)) {
			/* If the stop bit is requested, set it */
			if (msg->flags & I2C_MSG_STOP) {
				command |= HYDROGEN_CMD_STOP;
			}
		}
		command |= msg->buf[i] & 0xFF;

		i2c_hydrogen_busy(dev);

		i2c->read_write = command;

		i2c_hydrogen_wait(dev);

		if (i2c->read_write & HYDROGEN_RSP_ERROR) {
			LOG_ERR("I2C Rx failed to acknowledge\n");
			return -EIO;
		}
	}

	return 0;
}

static int i2c_hydrogen_read_msg(const struct device *dev,
				 struct i2c_msg *msg,
				 uint16_t addr)
{
	volatile struct i2c_hydrogen_regs *i2c = DEV_I2C(dev);
	uint32_t command;
	uint32_t result;
	int rc = 0;

	rc = i2c_hydrogen_send_addr(dev, HYDROGEN_READ, addr);
	if (rc != 0) {
		LOG_ERR("I2C failed to write message\n");
		return rc;
	}

	for (int i = 0; i < msg->len; i++) {
		command = 0 | HYDROGEN_CMD_READ;
		/* On the last byte of the message */
		if (i == (msg->len - 1)) {
			/* Set NACK to end read */
			command |= HYDROGEN_CMD_NACK;

			/* If the stop bit is requested, set it */
			if (msg->flags & I2C_MSG_STOP) {
				command |= HYDROGEN_CMD_STOP;
			}
		} else {
			command |= HYDROGEN_CMD_ACK;
		}

		i2c_hydrogen_busy(dev);

		i2c->read_write = command;

		i2c_hydrogen_wait(dev);

		result = i2c->read_write;
		if (result & HYDROGEN_RSP_ERROR) {
			LOG_ERR("I2C Rx failed to acknowledge\n");
			return -EIO;
		}
		msg->buf[i] = result & 0xFF;
	}

	return 0;
}

static int i2c_hydrogen_configure(const struct device *dev,
				  uint32_t dev_config)
{
	volatile struct i2c_hydrogen_regs *i2c = DEV_I2C(dev);
	volatile struct i2c_hydrogen_config *cfg = DEV_I2C_CFG(dev);
	uint32_t i2c_speed = 0U;

	/* Configure bus frequency */
	switch (I2C_SPEED_GET(dev_config)) {
	case I2C_SPEED_STANDARD:
		i2c_speed = 100000U; /* 100 KHz */
		break;
	case I2C_SPEED_FAST:
		i2c_speed = 400000U; /* 400 KHz */
		break;
	case I2C_SPEED_FAST_PLUS:
	case I2C_SPEED_HIGH:
	case I2C_SPEED_ULTRA:
	default:
		LOG_ERR("Unsupported I2C speed requested");
		return -ENOTSUP;
	}

	i2c->clock_div = cfg->sys_clk_freq / i2c_speed / 4;

	/* Support I2C Master mode only */
	if (!(dev_config & I2C_MODE_MASTER)) {
		LOG_ERR("I2C only supports operation as master");
		return -ENOTSUP;
	}

	/*
	 * Driver does not support 10-bit addressing. This can be added
	 * in the future when needed.
	 */
	if (dev_config & I2C_ADDR_10_BITS) {
		LOG_ERR("I2C driver does not support 10-bit addresses");
		return -ENOTSUP;
	}

	return 0;
}

static int i2c_hydrogen_transfer(const struct device *dev,
				 struct i2c_msg *msgs,
				 uint8_t num_msgs,
				 uint16_t addr)
{
	int rc = 0;

	for (int i = 0; i < num_msgs; i++) {
		if (msgs[i].flags & I2C_MSG_READ) {
			rc = i2c_hydrogen_read_msg(dev, &msgs[i], addr);
		} else {
			rc = i2c_hydrogen_write_msg(dev, &msgs[i], addr);
		}

		if (rc != 0) {
			LOG_ERR("I2C failed to transfer messages\n");
			return rc;
		}
	}

	return 0;
}

static int i2c_hydrogen_init(const struct device *dev)
{
	volatile struct i2c_hydrogen_regs *i2c = DEV_I2C(dev);
	volatile struct i2c_hydrogen_config *cfg = DEV_I2C_CFG(dev);
	uint32_t dev_config = 0U;
	int rc = 0;

	/* Save fifo size to check later if busy */
	cfg->fifo_size = i2c->status >> 16;

	dev_config = (I2C_MODE_MASTER | i2c_map_dt_bitrate(cfg->bus_clk_freq));

	rc = i2c_hydrogen_configure(dev, dev_config);
	if (rc != 0) {
		LOG_ERR("Failed to configure I2C on init");
		return rc;
	}

	return 0;
}

static const struct i2c_driver_api i2c_hydrogen_driver_api = {
	.configure = i2c_hydrogen_configure,
	.transfer = i2c_hydrogen_transfer
};


#if DT_NODE_EXISTS(DT_INST(0, hydrogen_i2c))
	I2C(0)
#endif

#if DT_NODE_EXISTS(DT_INST(1, hydrogen_i2c))
	I2C(1)
#endif

#if DT_NODE_EXISTS(DT_INST(2, hydrogen_i2c))
	I2C(2)
#endif

#if DT_NODE_EXISTS(DT_INST(3, hydrogen_i2c))
	I2C(3)
#endif

#if DT_NODE_EXISTS(DT_INST(4, hydrogen_i2c))
	I2C(4)
#endif
