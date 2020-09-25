/*
 * Copyright (c) 2020 Phytec Messtechnik GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file GPIO driver for the Hydrogen
 */

#include <errno.h>
#include <kernel.h>
#include <device.h>
#include <soc.h>
#include <drivers/gpio.h>
#include <sys/util.h>
#include "gpio_utils.h"

typedef void (*hydrogen_cfg_func_t)(void);

struct gpio_hydrogen_config {
	uint32_t regs;
	hydrogen_cfg_func_t cfg_func;
};

struct gpio_hydrogen_regs {
	unsigned int read;
	unsigned int write;
	unsigned int direction;
	unsigned int reserved1;
	unsigned int high_ip;
	unsigned int high_ie;
	unsigned int low_ip;
	unsigned int low_ie;
	unsigned int rise_ip;
	unsigned int rise_ie;
	unsigned int fall_ip;
	unsigned int fall_ie;
};

struct gpio_hydrogen_data {
	/* list of callbacks */
	sys_slist_t cb;
};

#define DEV_GPIO_CFG(dev)                                                      \
	((struct gpio_hydrogen_config *)(dev)->config)
#define DEV_GPIO(dev) ((struct gpio_hydrogen_regs *)(DEV_GPIO_CFG(dev))->regs)
#define DEV_GPIO_DATA(dev) ((struct gpio_hydrogen_data *)(dev)->data)

#ifdef GPIO_HYDROGEN_INTERRUPT
#define GPIO(no)							     \
	static struct gpio_hydrogen_data gpio_hydrogen_dev_data_##no;	     \
	static void gpio_hydrogen_irq_cfg_func_##no(void);		     \
	static struct gpio_hydrogen_config gpio_hydrogen_dev_cfg_##no = {    \
		.regs = DT_REG_ADDR(DT_INST(no, hydrogen_gpio)),	     \
		.cfg_func = gpio_hydrogen_irq_cfg_func_##no,		     \
	};								     \
	DEVICE_AND_API_INIT(gpio_hydrogen_##no,				     \
			    DT_PROP(DT_INST(no, hydrogen_gpio), label),	     \
			    gpio_hydrogen_init,				     \
			    &gpio_hydrogen_dev_data_##no,		     \
			    &gpio_hydrogen_dev_cfg_##no,		     \
			    PRE_KERNEL_1,				     \
			    CONFIG_KERNEL_INIT_PRIORITY_DEVICE,		     \
			    (void *)&gpio_hydrogen_driver_api);		     \
	static void gpio_hydrogen_irq_cfg_func_##no(void) {		     \
	IRQ_CONNECT(RISCV_MAX_GENERIC_IRQ +				     \
		    DT_IRQN(DT_INST(no, hydrogen_gpio)),		     \
		    0,							     \
		    gpio_hydrogen_irq_handler,				     \
		    DEVICE_GET(gpio_hydrogen_##no),			     \
		    0);							     \
	irq_enable(RISCV_MAX_GENERIC_IRQ +				     \
			DT_IRQN(DT_INST(no, hydrogen_gpio)));		     \
	}
#else
#define GPIO(no)							     \
	static struct gpio_hydrogen_data gpio_hydrogen_dev_data_##no;	     \
	static struct gpio_hydrogen_config gpio_hydrogen_dev_cfg_##no = {    \
		.regs = DT_REG_ADDR(DT_INST(no, hydrogen_gpio)),	     \
	};								     \
	DEVICE_AND_API_INIT(gpio_hydrogen_##no,				     \
			    DT_PROP(DT_INST(no, hydrogen_gpio), label),	     \
			    gpio_hydrogen_init,				     \
			    &gpio_hydrogen_dev_data_##no,		     \
			    &gpio_hydrogen_dev_cfg_##no,		     \
			    PRE_KERNEL_1,				     \
			    CONFIG_KERNEL_INIT_PRIORITY_DEVICE,		     \
			    (void *)&gpio_hydrogen_driver_api);
#endif


/**
 * @brief Configure pin
 *
 * @param dev Device structure
 * @param pin The pin number
 * @param flags Flags of pin or port
 *
 * @return 0 if successful, failed otherwise
 */
static int gpio_hydrogen_config(const struct device *dev, gpio_pin_t pin,
				gpio_flags_t flags)
{
	volatile struct gpio_hydrogen_regs *gpio = DEV_GPIO(dev);

	/* Configure gpio direction */
	if (flags & GPIO_OUTPUT)
		gpio->direction |= BIT(pin);
	else
		gpio->direction &= ~BIT(pin);

	if (!(flags & GPIO_INT_ENABLE))
		return 0;

	if (flags & GPIO_OUTPUT)
		return -EINVAL;

	/* Edge or Level triggered ? */
	if (flags & GPIO_INT_EDGE) {
		gpio->high_ie &= ~BIT(pin);
		gpio->low_ie &= ~BIT(pin);

		/* Rising Edge, Falling Edge or Double Edge ? */
		if (flags & GPIO_INT_EDGE_BOTH) { // double edge
			gpio->rise_ie |= BIT(pin);
			gpio->fall_ie |= BIT(pin);
		} else if (flags & GPIO_INT_EDGE_RISING) { // rising edge
			gpio->rise_ie |= BIT(pin);
			gpio->fall_ie &= ~BIT(pin);
		} else { // falling edge
			gpio->rise_ie &= ~BIT(pin);
			gpio->fall_ie |= BIT(pin);
		}
	} else {
		gpio->rise_ie &= ~BIT(pin);
		gpio->fall_ie &= ~BIT(pin);

		/* Level High ? */
		if (flags & GPIO_INT_LEVEL_HIGH) {
			gpio->high_ie |= BIT(pin);
			gpio->low_ie &= ~BIT(pin);
		} else {
			gpio->high_ie &= ~BIT(pin);
			gpio->low_ie |= BIT(pin);
		}
	}

	return 0;
}


static int gpio_hydrogen_port_get_raw(const struct device *dev,
				   gpio_port_value_t *value)
{
	volatile struct gpio_hydrogen_regs *gpio = DEV_GPIO(dev);

	*value = gpio->read;

	return 0;
}

static int gpio_hydrogen_port_set_masked_raw(const struct device *dev,
					  gpio_port_pins_t mask,
					  gpio_port_value_t value)
{
	volatile struct gpio_hydrogen_regs *gpio = DEV_GPIO(dev);

	gpio->write = (gpio->write & ~mask) | (value & mask);

	return 0;
}

static int gpio_hydrogen_port_set_bits_raw(const struct device *dev,
					gpio_port_pins_t mask)
{
	volatile struct gpio_hydrogen_regs *gpio = DEV_GPIO(dev);

	gpio->write |= mask;

	return 0;
}

static int gpio_hydrogen_port_clear_bits_raw(const struct device *dev,
					  gpio_port_pins_t mask)
{
	volatile struct gpio_hydrogen_regs *gpio = DEV_GPIO(dev);

	gpio->write &= ~mask;

	return 0;
}

static int gpio_hydrogen_port_toggle_bits(const struct device *dev,
				       gpio_port_pins_t mask)
{
	volatile struct gpio_hydrogen_regs *gpio = DEV_GPIO(dev);

	gpio->write ^= mask;

	return 0;
}

#ifdef GPIO_HYDROGEN_INTERRUPT

static void gpio_hydrogen_irq_handler(void *arg)
{
	struct device *dev = (struct device *)arg;
	struct gpio_hydrogen_data *data = DEV_GPIO_DATA(dev);
	volatile struct gpio_hydrogen_regs *gpio = DEV_GPIO(dev);
	const struct gpio_hydrogen_config *cfg = DEV_GPIO_CFG(dev);
	int pin_mask;

	/* Get the pin number generating the interrupt */
	pin_mask = 1 << (riscv_plic_get_irq() -
			 (cfg->irq_base - RISCV_MAX_GENERIC_IRQ));

	/* Call the corresponding callback registered for the pin */
	_gpio_fire_callbacks(&data->cb, dev, pin_mask);

	/*
	 * Write to either the rise_ip, fall_ip, high_ip or low_ip registers
	 * to indicate to GPIO controller that interrupt for the corresponding
	 * pin has been handled.
	 */
	if (gpio->rise_ip & pin_mask)
		gpio->rise_ip = pin_mask;
	else if (gpio->fall_ip & pin_mask)
		gpio->fall_ip = pin_mask;
	else if (gpio->high_ip & pin_mask)
		gpio->high_ip = pin_mask;
	else if (gpio->low_ip & pin_mask)
		gpio->low_ip = pin_mask;
}

static int gpio_hydrogen_manage_callback(const struct device *dev,
					 struct gpio_callback *callback,
					 bool set)
{
	struct gpio_hydrogen_data *data = DEV_GPIO_DATA(dev);

	return gpio_manage_callback(&data->cb, callback, set);
}

static int gpio_hydrogen_enable_callback(struct device *dev, gpio_pin_t pin)
{
	const struct gpio_hydrogen_config *cfg = DEV_GPIO_CFG(dev);

	// Enable interrupt for the pin at PLIC level
	irq_enable(cfg->irq_base + pin);

	return 0;
}

static int gpio_hydrogen_disable_callback(struct device *dev, gpio_pin_t pin)
{
	const struct gpio_hydrogen_config *cfg = DEV_GPIO_CFG(dev);

	// Disable interrupt for the pin at PLIC level
	irq_disable(cfg->irq_base + pin);

	return 0;
}

#endif /* CONFIG_GPIO_INTERRUPT */

/**
 * @brief Initialize a GPIO controller
 *
 * Perform basic initialization of a GPIO controller
 *
 * @param dev GPIO device struct
 *
 * @return 0
 */
static int gpio_hydrogen_init(const struct device *dev)
{
	volatile struct gpio_hydrogen_regs *gpio = DEV_GPIO(dev);
#ifdef GPIO_HYDROGEN_INTERRUPT
	volatile struct gpio_hydrogen_config *cfg = DEV_GPIO_CFG(dev);
#endif

	gpio->write = 0;
	gpio->direction = 0;
	gpio->high_ie = 0;
	gpio->low_ie = 0;
	gpio->rise_ie = 0;
	gpio->fall_ie = 0;

#ifdef GPIO_HYDROGEN_INTERRUPT
	cfg->cfg_func();
#endif

	return 0;
}

static const struct gpio_driver_api gpio_hydrogen_driver_api = {
	.pin_configure = gpio_hydrogen_config,
	.port_get_raw = gpio_hydrogen_port_get_raw,
	.port_set_masked_raw = gpio_hydrogen_port_set_masked_raw,
	.port_set_bits_raw = gpio_hydrogen_port_set_bits_raw,
	.port_clear_bits_raw = gpio_hydrogen_port_clear_bits_raw,
	.port_toggle_bits = gpio_hydrogen_port_toggle_bits,
#ifdef GPIO_HYDROGEN_INTERRUPT
	.manage_callback = gpio_hydrogen_manage_callback,
	.enable_callback = gpio_hydrogen_enable_callback,
	.disable_callback = gpio_hydrogen_disable_callback,
#endif
};


#if DT_NODE_EXISTS(DT_INST(0, hydrogen_gpio))
	GPIO(0)
#endif
#if DT_NODE_EXISTS(DT_INST(1, hydrogen_gpio))
	GPIO(1)
#endif
#if DT_NODE_EXISTS(DT_INST(2, hydrogen_gpio))
	GPIO(2)
#endif
#if DT_NODE_EXISTS(DT_INST(3, hydrogen_gpio))
	GPIO(3)
#endif
#if DT_NODE_EXISTS(DT_INST(4, hydrogen_gpio))
	GPIO(4)
#endif
