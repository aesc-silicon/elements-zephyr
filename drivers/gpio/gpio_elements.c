/*
 * Copyright (c) 2023 aesc silicon
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file GPIO driver for the Elements
 */

#define DT_DRV_COMPAT elements_gpio

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <soc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>

#include <zephyr/drivers/gpio/gpio_utils.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(DT_DRV_COMPAT);

typedef void (*elements_cfg_func_t)(void);

struct gpio_elements_config {
	/* gpio_driver_config needs to be first */
	struct gpio_driver_config common;
	uint32_t regs;
	elements_cfg_func_t cfg_func;
	uint32_t irq_no;
};

struct gpio_elements_regs {
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

struct gpio_elements_data {
	/* gpio_driver_data needs to be first */
	struct gpio_driver_data common;
	/* list of callbacks */
	sys_slist_t cb;
};

#define DEV_GPIO_CFG(dev)                                                      \
	((struct gpio_elements_config *)(dev)->config)
#define DEV_GPIO(dev) ((struct gpio_elements_regs *)(DEV_GPIO_CFG(dev))->regs)
#define DEV_GPIO_DATA(dev) ((struct gpio_elements_data *)(dev)->data)

#ifdef CONFIG_GPIO_ELEMENTS_INTERRUPT
#define ELEMENTS_GPIO_INIT(no)						     \
	static struct gpio_elements_data gpio_elements_dev_data_##no;	     \
	static void gpio_elements_irq_cfg_func_##no(void);		     \
	static struct gpio_elements_config gpio_elements_dev_cfg_##no = {    \
		.regs = DT_REG_ADDR(DT_INST(no, elements_gpio)),	     \
		.cfg_func = gpio_elements_irq_cfg_func_##no,		     \
		.irq_no = DT_IRQN(DT_INST(no, elements_gpio)),		     \
	};								     \
	DEVICE_DT_INST_DEFINE(no,					     \
			      gpio_elements_init,			     \
			      NULL,					     \
			      &gpio_elements_dev_data_##no,		     \
			      &gpio_elements_dev_cfg_##no,		     \
			      PRE_KERNEL_1,				     \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,	     \
			      (void *)&gpio_elements_driver_api);	     \
	static void gpio_elements_irq_cfg_func_##no(void) {		     \
	IRQ_CONNECT(CONFIG_2ND_LVL_ISR_TBL_OFFSET +			     \
		    DT_IRQN(DT_INST(no, elements_gpio)),		     \
		    0,							     \
		    gpio_elements_irq_handler,				     \
		    DEVICE_DT_INST_GET(no),				     \
		    0);							     \
	}
#else
#define ELEMENTS_GPIO_INIT(no)						     \
	static struct gpio_elements_data gpio_elements_dev_data_##no;	     \
	static struct gpio_elements_config gpio_elements_dev_cfg_##no = {    \
		.regs = DT_REG_ADDR(DT_INST(no, elements_gpio)),	     \
	};								     \
	DEVICE_DT_INST_DEFINE(no,					     \
			      gpio_elements_init,			     \
			      NULL,					     \
			      &gpio_elements_dev_data_##no,		     \
			      &gpio_elements_dev_cfg_##no,		     \
			      PRE_KERNEL_1,				     \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,	     \
			      (void *)&gpio_elements_driver_api);
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

#ifdef CONFIG_GPIO_ELEMENTS_INTERRUPT

static void gpio_elements_irq_handler(void *arg)
{
	struct device *dev = (struct device *)arg;
	struct gpio_elements_data *data = DEV_GPIO_DATA(dev);
	volatile struct gpio_elements_regs *gpio = DEV_GPIO(dev);
	uint32_t pin_mask = 0;

	pin_mask |= gpio->rise_ip;
	pin_mask |= gpio->fall_ip;
	pin_mask |= gpio->high_ip;
	pin_mask |= gpio->low_ip;

	for (int pin = 0; pin < 32; pin++) {
		if (pin_mask & (0x1 << pin)) {
			/* Fire callback for pin */
			gpio_fire_callbacks(&data->cb, dev, BIT(pin));
			/*
			 * Write to either the rise_ip, fall_ip, high_ip or
			 * low_ip registers to indicate to GPIO controller
			 * that interrupt for the corresponding pin has been
			 * handled.
			 */
			gpio->rise_ip = BIT(pin);
			gpio->fall_ip = BIT(pin);
			gpio->high_ip = BIT(pin);
			gpio->low_ip = BIT(pin);
		}
	}
}

static int gpio_elements_pin_interrupt_configure(const struct device *dev,
						 gpio_pin_t pin,
						 enum gpio_int_mode int_mode,
						 enum gpio_int_trig int_trig)
{
	volatile struct gpio_elements_regs *gpio = DEV_GPIO(dev);
	volatile struct gpio_elements_config *cfg = DEV_GPIO_CFG(dev);

	/* Disable all interrupts */
	gpio->high_ie &= ~BIT(pin);
	gpio->low_ie &= ~BIT(pin);
	gpio->rise_ie &= ~BIT(pin);
	gpio->fall_ie &= ~BIT(pin);

	/* Disable interrupt for the pin at PLIC level */
	if (int_mode & GPIO_INT_MODE_DISABLED) {
		irq_disable(irq_to_level_2(cfg->irq_no));
		return 0;
	}

	if (int_mode & GPIO_INT_EDGE) {
		/* Clear pending pits */
		gpio->rise_ip = BIT(pin);
		gpio->fall_ip = BIT(pin);

		/* Double edge */
		if ((int_trig & GPIO_INT_TRIG_BOTH) == GPIO_INT_TRIG_BOTH) {
			gpio->rise_ie |= BIT(pin);
			gpio->fall_ie |= BIT(pin);
		/* Rising edge */
		} else if (int_trig & GPIO_INT_TRIG_HIGH) {
			gpio->rise_ie |= BIT(pin);
		/* Falling edge */
		} else {
			gpio->fall_ie |= BIT(pin);
		}
	} else {
		/* Clear pending pits */
		gpio->high_ip = BIT(pin);
		gpio->low_ip = BIT(pin);

		/* Level High ? */
		if (int_trig & GPIO_INT_TRIG_HIGH) {
			gpio->high_ie |= BIT(pin);
		} else {
			gpio->low_ie |= BIT(pin);
		}
	}
	irq_enable(irq_to_level_2(cfg->irq_no));

	return 0;
}

static int gpio_elements_manage_callback(const struct device *dev,
					 struct gpio_callback *callback,
					 bool set)
{
	struct gpio_elements_data *data = DEV_GPIO_DATA(dev);

	return gpio_manage_callback(&data->cb, callback, set);
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
static int gpio_elements_init(const struct device *dev)
{
	volatile struct gpio_elements_regs *gpio = DEV_GPIO(dev);
#ifdef CONFIG_GPIO_ELEMENTS_INTERRUPT
	volatile struct gpio_elements_config *cfg = DEV_GPIO_CFG(dev);
#endif

	gpio->write = 0;
	gpio->direction = 0;
	gpio->high_ie = 0;
	gpio->low_ie = 0;
	gpio->rise_ie = 0;
	gpio->fall_ie = 0;

#ifdef CONFIG_GPIO_ELEMENTS_INTERRUPT
	gpio->high_ip = 0;
	gpio->low_ip = 0;
	gpio->rise_ip = 0;
	gpio->fall_ip = 0;

	cfg->cfg_func();
#endif

	return 0;
}

static const struct gpio_driver_api gpio_elements_driver_api = {
	.pin_configure = gpio_elements_config,
	.port_get_raw = gpio_elements_port_get_raw,
	.port_set_masked_raw = gpio_elements_port_set_masked_raw,
	.port_set_bits_raw = gpio_elements_port_set_bits_raw,
	.port_clear_bits_raw = gpio_elements_port_clear_bits_raw,
	.port_toggle_bits = gpio_elements_port_toggle_bits,
#ifdef CONFIG_GPIO_ELEMENTS_INTERRUPT
	.pin_interrupt_configure = gpio_elements_pin_interrupt_configure,
	.manage_callback = gpio_elements_manage_callback,
#endif
};

DT_INST_FOREACH_STATUS_OKAY(ELEMENTS_GPIO_INIT)
