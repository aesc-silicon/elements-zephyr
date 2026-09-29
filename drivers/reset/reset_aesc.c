/*
 * SPDX-FileCopyrightText: 2026 Aesc Silicon
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT aesc_reset_controller

#include <errno.h>
#include <aesc_reset.h>
#include <ip_identification.h>
#include <soc.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/sys_io.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(aesc_reset);

#define RESET_AESC_DOMAINS	0x00
#define RESET_AESC_ENABLE	0x04
#define RESET_AESC_TRIGGER	0x08
#define RESET_AESC_ACKNOWLEDGE	0x0c

#define RESET_AESC_DOMAINS_MASK	GENMASK(7, 0)

struct reset_aesc_config {
	DEVICE_MMIO_NAMED_ROM(mmio);
};

struct reset_aesc_data {
	DEVICE_MMIO_NAMED_RAM(mmio);

	uintptr_t reg_base;
	uint32_t valid_mask;
	struct k_spinlock lock;
};

#define DEV_CFG(dev) ((const struct reset_aesc_config * const)(dev)->config)
#define DEV_DATA(dev) ((struct reset_aesc_data *)(dev)->data)

int reset_aesc_toggle_mask(const struct device *dev, uint32_t mask)
{
	struct reset_aesc_data *data = DEV_DATA(dev);
	k_spinlock_key_t key;

	if ((mask == 0U) || ((mask & ~data->valid_mask) != 0U)) {
		return -EINVAL;
	}

	/* Each domain is reset for its configured delay once the trigger is acknowledged */
	key = k_spin_lock(&data->lock);
	sys_write32(mask, data->reg_base + RESET_AESC_TRIGGER);
	sys_write32(1U, data->reg_base + RESET_AESC_ACKNOWLEDGE);
	k_spin_unlock(&data->lock, key);

	return 0;
}

static int reset_aesc_line_toggle(const struct device *dev, uint32_t id)
{
	if (id >= 32U) {
		return -EINVAL;
	}

	return reset_aesc_toggle_mask(dev, BIT(id));
}

static int reset_aesc_init(const struct device *dev)
{
	DEVICE_MMIO_NAMED_MAP(dev, mmio, K_MEM_CACHE_NONE);
	volatile uintptr_t *base_addr =
		(volatile uintptr_t *)DEVICE_MMIO_NAMED_GET(dev, mmio);
	struct reset_aesc_data *data = DEV_DATA(dev);
	uint32_t domains;

	if (ip_id_get_id(base_addr) != IP_ID_RESET) {
		LOG_ERR("Unexpected IP core ID %u.", ip_id_get_id(base_addr));
		return -ENODEV;
	}

	LOG_DBG("IP core version: %i.%i.%i.",
		ip_id_get_major_version(base_addr),
		ip_id_get_minor_version(base_addr),
		ip_id_get_patchlevel(base_addr)
	);
	data->reg_base = ip_id_relocate_driver(base_addr);
	LOG_DBG("Relocate driver to address 0x%lx.", data->reg_base);

	domains = sys_read32(data->reg_base + RESET_AESC_DOMAINS) & RESET_AESC_DOMAINS_MASK;
	data->valid_mask = (domains >= 32U) ? UINT32_MAX : BIT_MASK(domains);

	return 0;
}

static DEVICE_API(reset, reset_aesc_driver_api) = {
	.line_toggle = reset_aesc_line_toggle,
};

#define AESC_RESET_INIT(no)						      \
	static struct reset_aesc_data reset_aesc_dev_data_##no;		      \
	static const struct reset_aesc_config reset_aesc_dev_cfg_##no = {     \
		DEVICE_MMIO_NAMED_ROM_INIT(mmio, DT_DRV_INST(no)),	      \
	};								      \
	DEVICE_DT_INST_DEFINE(no,					      \
			      reset_aesc_init,				      \
			      NULL,					      \
			      &reset_aesc_dev_data_##no,		      \
			      &reset_aesc_dev_cfg_##no,			      \
			      PRE_KERNEL_1,				      \
			      CONFIG_RESET_INIT_PRIORITY,		      \
			      &reset_aesc_driver_api);

DT_INST_FOREACH_STATUS_OKAY(AESC_RESET_INIT)
