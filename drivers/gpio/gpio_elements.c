/*
 * Copyright (c) 2025 aesc silicon
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file GPIO driver for the Elements
 */

#define DT_DRV_COMPAT elements_gpio

#include <errno.h>
#include <soc.h>

#include <zephyr/arch/common/sys_bitops.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>

#include <elements/drivers/ip_identification.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(elements_gpio, CONFIG_GPIO_LOG_LEVEL);

typedef void (*elements_cfg_func_t)(void);

struct gpio_elements_config {
	void (*bank_config)(const struct device *dev);
	struct gpio_driver_config common;

	DEVICE_MMIO_NAMED_ROM(regs);
};

struct gpio_elements_regs {
	uint32_t info;
	uint32_t read;
	uint32_t write;
	uint32_t direction;
	uint32_t high_ip;
	uint32_t high_ie;
	uint32_t low_ip;
	uint32_t low_ie;
	uint32_t rise_ip;
	uint32_t rise_ie;
	uint32_t fall_ip;
	uint32_t fall_ie;
};

struct gpio_elements_data {
	/* gpio_driver_data needs to be first */
	struct gpio_driver_data common;
	/* list of callbacks */
	sys_slist_t cb;
};

#define DEV_CFG(dev)						      \
	((struct gpio_elements_config *)(dev)->config)
#define DEV_DATA(dev) ((struct gpio_elements_data *)(dev)->data)
#define DEV_GPIO(dev) 							      \
	((struct gpio_elements_regs *)DEVICE_MMIO_NAMED_GET(dev, regs))

static int gpio_elements_config(const struct device *dev, gpio_pin_t pin,
				gpio_flags_t flags)
{
	volatile struct gpio_elements_regs *gpio = DEV_GPIO(dev);

	/* Configure gpio direction */
	if (flags & GPIO_OUTPUT)
		gpio->direction |= BIT(pin);
	else
		gpio->direction &= ~BIT(pin);

	return 0;
}


static int gpio_elements_port_get_raw(const struct device *dev,
				   gpio_port_value_t *value)
{
	volatile struct gpio_elements_regs *gpio = DEV_GPIO(dev);

	*value = gpio->read;

	return 0;
}

static int gpio_elements_port_set_masked_raw(const struct device *dev,
					  gpio_port_pins_t mask,
					  gpio_port_value_t value)
{
	volatile struct gpio_elements_regs *gpio = DEV_GPIO(dev);

	gpio->write = (gpio->write & ~mask) | (value & mask);

	return 0;
}

static int gpio_elements_port_set_bits_raw(const struct device *dev,
					gpio_port_pins_t mask)
{
	volatile struct gpio_elements_regs *gpio = DEV_GPIO(dev);

	gpio->write |= mask;

	return 0;
}

static int gpio_elements_port_clear_bits_raw(const struct device *dev,
					  gpio_port_pins_t mask)
{
	volatile struct gpio_elements_regs *gpio = DEV_GPIO(dev);

	gpio->write &= ~mask;

	return 0;
}

static int gpio_elements_port_toggle_bits(const struct device *dev,
				       gpio_port_pins_t mask)
{
	volatile struct gpio_elements_regs *gpio = DEV_GPIO(dev);

	gpio->write ^= mask;

	return 0;
}

static int gpio_elements_init(const struct device *dev)
{
	volatile uintptr_t *base_addr = (volatile uintptr_t *)DEV_GPIO(dev);
	volatile struct gpio_elements_regs *gpio;

	DEVICE_MMIO_NAMED_MAP(dev, regs, K_MEM_CACHE_NONE);
	LOG_INF("IP core version: %i.%i.%i.",
		ip_id_get_major_version(base_addr),
		ip_id_get_minor_version(base_addr),
		ip_id_get_patchlevel(base_addr)
	);
	DEVICE_MMIO_NAMED_GET(dev, regs) = ip_id_relocate_driver(base_addr);
	LOG_INF("Relocate driver to address 0x%lx.", DEVICE_MMIO_NAMED_GET(dev, regs));
	gpio = DEV_GPIO(dev);

	gpio->high_ie = 0;
	gpio->low_ie = 0;
	gpio->rise_ie = 0;
	gpio->fall_ie = 0;

	return 0;
}

static const struct gpio_driver_api gpio_elements_driver_api = {
	.pin_configure = gpio_elements_config,
	.port_get_raw = gpio_elements_port_get_raw,
	.port_set_masked_raw = gpio_elements_port_set_masked_raw,
	.port_set_bits_raw = gpio_elements_port_set_bits_raw,
	.port_clear_bits_raw = gpio_elements_port_clear_bits_raw,
	.port_toggle_bits = gpio_elements_port_toggle_bits,
};

#define GPIO_ELEMENTS_INIT_FUNC(no)						  \
	static void gpio_elements_bank_##no##_config(const struct device *dev)	  \
	{									  \
		volatile struct gpio_elements_regs *regs = DEV_GPIO(dev);	  \
		ARG_UNUSED(regs);						  \
	}

#define ELEMENTS_GPIO_INIT(no)						     \
	GPIO_ELEMENTS_INIT_FUNC(no);					     \
	static struct gpio_elements_data gpio_elements_dev_data_##no;	     \
	static struct gpio_elements_config gpio_elements_dev_cfg_##no = {    \
		.bank_config = gpio_elements_bank_##no##_config,	     \
		.common = {						     \
			.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_DT_INST(no),\
		},							     \
		DEVICE_MMIO_NAMED_ROM_INIT(regs, DT_DRV_INST(no)),	     \
	};								     \
	DEVICE_DT_INST_DEFINE(no,					     \
			      gpio_elements_init,			     \
			      NULL,					     \
			      &gpio_elements_dev_data_##no,		     \
			      &gpio_elements_dev_cfg_##no,		     \
			      PRE_KERNEL_2,				     \
			      CONFIG_GPIO_INIT_PRIORITY,		     \
			      (void *)&gpio_elements_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ELEMENTS_GPIO_INIT)
