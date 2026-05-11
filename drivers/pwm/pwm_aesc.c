/*
 * SPDX-FileCopyrightText: 2026 Aesc Silicon
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT aesc_pwm

#include <errno.h>
#include <ip_identification.h>
#include <soc.h>

#include <zephyr/device.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/irq.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(aesc_pwm, CONFIG_PWM_LOG_LEVEL);

/*
 * Register map (relative to ip_id_relocate_driver base):
 *
 *   0x00  channel_config  channelPeriodWidth[31:24], channelPulseWidth[23:16],
 *                         clockDividerWidth[15:8], channels[7:0]
 *   0x04  timing_config   shotCountWidth[15:8], deadTimeWidth[7:0]
 *   0x08  permission      busCanWriteClockDividerConfig [0]
 *   0x0C  irq_ip          period-complete pending, one bit per channel (W1C)
 *   0x10  irq_ie          period-complete enable mask
 *   0x14  error_ip        error pending: faultIn[0], configError[channels:1]
 *   0x18  error_ie        error enable mask
 *   0x1C + n*0x24 channel n:
 *     +0x00  control      enable[0], invert[1], mode[2]
 *     +0x04  clock_div
 *     +0x08  period
 *     +0x0C  rising_edge
 *     +0x10  falling_edge
 *     +0x14  dead_time
 *     +0x18  phase_offset
 *     +0x1C  shot_count
 *     +0x20  status       configError[0], shotDone[1]
 */

#define AESC_PWM_CTRL_ENABLE	BIT(0)
#define AESC_PWM_CTRL_INVERT	BIT(1)
#define AESC_PWM_CTRL_MODE	BIT(2)	/* 0=edge-aligned, 1=center-aligned */

#define AESC_PWM_WIDTHS_PERIOD_SHIFT	24
#define AESC_PWM_WIDTHS_DIV_SHIFT	8
#define AESC_PWM_WIDTHS_CHAN_MASK	GENMASK(7, 0)

#define AESC_PWM_CHANNEL_SIZE	0x1c

struct pwm_aesc_regs {
	uint32_t channel_config;
	uint32_t timing_config;
	uint32_t permission;
	uint32_t irq_ip;
	uint32_t irq_ie;
	uint32_t error_ip;
	uint32_t error_ie;
};

struct pwm_aesc_channel {
	uint32_t control;
	uint32_t clock_div;
	uint32_t period;
	uint32_t rising_edge;
	uint32_t falling_edge;
	uint32_t dead_time;
	uint32_t phase_offset;
	uint32_t shot_count;
	uint32_t status;
};

struct pwm_aesc_data {
	DEVICE_MMIO_RAM;
	uintptr_t reg_base;
	uint8_t channels;
	uint8_t period_width;
	uint8_t div_width;
};

struct pwm_aesc_config {
	DEVICE_MMIO_ROM;
	uint64_t sys_clk_freq;
	const struct pinctrl_dev_config *pcfg;
	void (*irq_config)(const struct device *dev);
};

#define DEV_CFG(dev)  ((struct pwm_aesc_config *)(dev)->config)
#define DEV_DATA(dev) ((struct pwm_aesc_data *)(dev)->data)
#define DEV_PWM(dev) \
	((volatile struct pwm_aesc_regs *)DEV_DATA(dev)->reg_base)

static inline volatile struct pwm_aesc_channel *
pwm_aesc_chan(const struct device *dev, uint32_t ch)
{
	/* Channel registers start at reg_base + 0x1C, stride 0x28 */
	return (volatile struct pwm_aesc_channel *)
		(DEV_DATA(dev)->reg_base + sizeof(struct pwm_aesc_regs) +
		 ch * sizeof(struct pwm_aesc_channel));
}

static int pwm_aesc_set_cycles(const struct device *dev, uint32_t channel,
			       uint32_t period_cycles, uint32_t pulse_cycles,
			       pwm_flags_t flags)
{
	struct pwm_aesc_data *data = DEV_DATA(dev);
	uint32_t max_period = (1U << data->period_width);
	uint32_t max_div = (1U << data->div_width) - 1U;
	volatile struct pwm_aesc_channel *ch;
	uint32_t div, scaled_period, scaled_pulse;
	uint32_t ctrl;

	if (channel >= data->channels) {
		return -EINVAL;
	}
	if (period_cycles == 0) {
		return -EINVAL;
	}
	if (pulse_cycles > period_cycles) {
		return -EINVAL;
	}

	/*
	 * Find the smallest clock divider so that period_cycles fits in the
	 * period register. With clock_div = N the hardware tick rate is
	 * sys_clk / (N + 1), giving period register = period_cycles / (N + 1).
	 * Both period and pulse are scaled by the same factor so the duty
	 * cycle is preserved within ±1 LSB.
	 */
	div = 0;
	scaled_period = period_cycles;
	while (scaled_period > max_period) {
		div++;
		if (div > max_div) {
			return -EINVAL;
		}
		scaled_period = period_cycles / (div + 1U);
	}

	scaled_pulse = pulse_cycles / (div + 1U);

	ch = pwm_aesc_chan(dev, channel);

	/*
	 * Disable first so the enable rising edge latches all shadow
	 * registers atomically on re-enable.
	 */
	ch->control = 0;

	if (pulse_cycles == 0) {
		/* Channel stays disabled; output held at safe idle level. */
		return 0;
	}

	/*
	 * Edge-aligned mode (count-down from scaled_period to 0).
	 * Output active when counter ∈ [rising_edge, falling_edge].
	 *
	 *   period       = scaled_period - 1
	 *   rising_edge  = scaled_period - scaled_pulse
	 *   falling_edge = scaled_period - 1
	 */
	ch->clock_div = div;
	ch->period = scaled_period - 1U;
	ch->rising_edge = scaled_period - scaled_pulse;
	ch->falling_edge = scaled_period - 1U;
	ch->dead_time = 0;
	ch->phase_offset = 0;
	ch->shot_count = 0;

	ctrl = AESC_PWM_CTRL_ENABLE;
	if (flags & PWM_POLARITY_INVERTED) {
		ctrl |= AESC_PWM_CTRL_INVERT;
	}
	ch->control = ctrl;

	return 0;
}

static int pwm_aesc_get_cycles_per_sec(const struct device *dev,
				       uint32_t channel, uint64_t *cycles)
{
	if (channel >= DEV_DATA(dev)->channels) {
		return -EINVAL;
	}

	/*
	 * Report the system clock as the reference. set_cycles() accepts
	 * period_cycles and pulse_cycles in sys-clock units and derives the
	 * per-channel clock divider internally, so callers always compute
	 * cycles against sys_clk_freq.
	 */
	*cycles = DEV_CFG(dev)->sys_clk_freq;
	return 0;
}

static void pwm_aesc_isr(const struct device *dev)
{
	volatile struct pwm_aesc_regs *pwm = DEV_PWM(dev);

	/* Clear all pending interrupts */
	pwm->irq_ip = pwm->irq_ip;
}

static int pwm_aesc_init(const struct device *dev)
{
	DEVICE_MMIO_MAP(dev, K_MEM_CACHE_NONE);
	const struct pwm_aesc_config *cfg = DEV_CFG(dev);
	volatile uintptr_t *base_addr =
		(volatile uintptr_t *)DEVICE_MMIO_GET(dev);
	struct pwm_aesc_data *data = DEV_DATA(dev);
	volatile struct pwm_aesc_regs *pwm;
	uint32_t channel_config;
	int ret;

	LOG_DBG("IP core version: %i.%i.%i.",
		ip_id_get_major_version(base_addr),
		ip_id_get_minor_version(base_addr),
		ip_id_get_patchlevel(base_addr)
	);
	data->reg_base = ip_id_relocate_driver(base_addr);
	LOG_DBG("Relocate driver to address 0x%lx.", data->reg_base);
	pwm = DEV_PWM(dev);

	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("failed to apply pinctrl");
		return ret;
	}

	channel_config = pwm->channel_config;
	data->channels = channel_config & AESC_PWM_WIDTHS_CHAN_MASK;
	data->period_width = (channel_config >> AESC_PWM_WIDTHS_PERIOD_SHIFT) & 0xFF;
	data->div_width = (channel_config >> AESC_PWM_WIDTHS_DIV_SHIFT) & 0xFF;

	/* Disable all interrupts and errors */
	pwm->irq_ie = 0;
	pwm->error_ie = 0;

	cfg->irq_config(dev);

	return 0;
}

static DEVICE_API(pwm, pwm_aesc_driver_api) = {
	.set_cycles = pwm_aesc_set_cycles,
	.get_cycles_per_sec = pwm_aesc_get_cycles_per_sec,
};

#define AESC_PWM_INIT(no)						     \
	PINCTRL_DT_INST_DEFINE(no);					     \
	static void pwm_aesc_irq_config_##no(const struct device *dev)	     \
	{								     \
		IRQ_CONNECT(DT_INST_IRQN(no),				     \
			    DT_INST_IRQ(no, priority),			     \
			    pwm_aesc_isr,				     \
			    DEVICE_DT_INST_GET(no),			     \
			    0);						     \
		irq_enable(DT_INST_IRQN(no));				     \
	}								     \
	static struct pwm_aesc_data pwm_aesc_dev_data_##no;		     \
	static const struct pwm_aesc_config pwm_aesc_dev_cfg_##no = {	     \
		DEVICE_MMIO_ROM_INIT(DT_DRV_INST(no)),			     \
		.sys_clk_freq =						     \
			DT_PROP(DT_INST(no, aesc_pwm), input_frequency),    \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(no),		     \
		.irq_config = pwm_aesc_irq_config_##no,			     \
	};								     \
	DEVICE_DT_INST_DEFINE(no,					     \
			      pwm_aesc_init,				     \
			      NULL,					     \
			      &pwm_aesc_dev_data_##no,			     \
			      &pwm_aesc_dev_cfg_##no,			     \
			      POST_KERNEL,				     \
			      CONFIG_PWM_INIT_PRIORITY,			     \
			      &pwm_aesc_driver_api);

DT_INST_FOREACH_STATUS_OKAY(AESC_PWM_INIT)
