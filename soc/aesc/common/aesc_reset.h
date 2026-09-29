/*
 * SPDX-FileCopyrightText: 2026 Aesc Silicon
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_AESC_AESC_RESET
#define ZEPHYR_SOC_AESC_AESC_RESET

#include <stdint.h>

#include <zephyr/device.h>

/**
 * @brief Reset several reset domains of an Aesc Silicon reset controller at once.
 *
 * All domains in @p mask are triggered with a single register write, so they
 * enter reset in the same cycle.
 *
 * @param dev Reset controller device.
 * @param mask Bit mask of reset domain identifiers.
 *
 * @retval 0 Reset triggered.
 * @retval -EINVAL Empty mask or mask contains a domain the controller does not have.
 */
int reset_aesc_toggle_mask(const struct device *dev, uint32_t mask);

#endif /* ZEPHYR_SOC_AESC_AESC_RESET */
