/*
 * Copyright (c) 2023 aesc silicon
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT elements_pwm
#define LOG_LEVEL CONFIG_PWM_LOG_LEVEL

#include <zephyr/kernel.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/drivers/pwm.h>
#include <soc.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(DT_DRV_COMPAT);

#define CYCLES_PER_SECOND	1000000
#define MAX_CHANNELS		32

struct pwm_elements_config {
	uint32_t regs;
	uint32_t clock_frequency;
	uint8_t channels;
};

struct pwm_elements_channel_regs {
	unsigned int control;
	unsigned int period_timer;
	unsigned int pulse_timer;
	unsigned int reserved;
};

struct pwm_elements_regs {
	unsigned int clock_div;
	unsigned int reserved1;
	unsigned int reserved2;
	unsigned int reserved3;
	struct pwm_elements_channel_regs channels[MAX_CHANNELS];
};

#define ELEMENTS_PWM_CHANNEL_ENABLE	0
#define ELEMENTS_PWM_CHANNEL_INVERT	1

#define DEV_PWM_CFG(dev)						       \
	((struct pwm_elements_config *)(dev)->config)
#define DEV_PWM(dev)							       \
	((struct pwm_elements_regs *)(DEV_PWM_CFG(dev))->regs)

#define PWM_ELEMENTS_INIT(no)						       \
	static const struct pwm_elements_config config##no = {		       \
		.regs = DT_REG_ADDR(DT_INST(no, elements_pwm)),		       \
		.clock_frequency = DT_INST_PROP(no, clock_frequency),	       \
		.channels = DT_INST_PROP(no, channels),			       \
	};								       \
									       \
	DEVICE_DT_INST_DEFINE(no, pwm_elements_init,			       \
			      NULL, NULL, &config##no,			       \
			      POST_KERNEL, CONFIG_PWM_INIT_PRIORITY,	       \
			      &pwm_elements_driver_api);

/* API implementation: set_cycles */
static int pwm_elements_set_cycles(const struct device *dev, uint32_t channel,
			      uint32_t period_cycles, uint32_t pulse_cycles,
			      pwm_flags_t flags)
{
	const struct pwm_elements_config *cfg = DEV_PWM_CFG(dev);
	struct pwm_elements_regs *pwm = DEV_PWM(dev);
	struct pwm_elements_channel_regs *channel_regs;
	uint32_t control;

	if (channel >= cfg->channels || channel > MAX_CHANNELS) {
		return -EINVAL;
	}

	channel_regs = &pwm->channels[channel];
	control = channel_regs->control;

	/* Select PWM inverted polarity (ie. active-low pulse). */
	if (flags & PWM_POLARITY_INVERTED) {
		control |= BIT(ELEMENTS_PWM_CHANNEL_INVERT);
	} else {
		control &= ~BIT(ELEMENTS_PWM_CHANNEL_INVERT);
	}

	/* If pulse_cycles is 0, switch PWM off and return. */
	if (pulse_cycles == 0) {
		control &= ~BIT(ELEMENTS_PWM_CHANNEL_ENABLE);
		channel_regs->control = control;
		return 0;
	}

	control |= BIT(ELEMENTS_PWM_CHANNEL_ENABLE);

	channel_regs->period_timer = period_cycles - 1;
	channel_regs->pulse_timer = pulse_cycles;
	channel_regs->control = control;

	return 0;
}

/* API implementation: get_cycles_per_sec */
static int pwm_elements_get_cycles_per_sec(const struct device *dev,
				      uint32_t channel, uint64_t *cycles)
{

	return CYCLES_PER_SECOND;
}

static int pwm_elements_init(const struct device *dev)
{
	const struct pwm_elements_config *cfg = DEV_PWM_CFG(dev);
	struct pwm_elements_regs *pwm = DEV_PWM(dev);

	pwm->clock_div = (cfg->clock_frequency / CYCLES_PER_SECOND) - 1;

	return 0;
}

/* PWM driver APIs structure */
static const struct pwm_driver_api pwm_elements_driver_api = {
	.set_cycles = pwm_elements_set_cycles,
	.get_cycles_per_sec = pwm_elements_get_cycles_per_sec,
};


DT_INST_FOREACH_STATUS_OKAY(PWM_ELEMENTS_INIT)
