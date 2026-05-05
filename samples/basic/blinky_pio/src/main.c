/*
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file Sample app to demonstrate PWM.
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/misc/pio_aesc/pio_aesc.h>

static const struct device *pio = DEVICE_DT_GET_ONE(aesc_pio);

/* Example 1: blink pin 0 forever */
static const struct pio_cmd blink_program[] = {
	{ PIO_CMD_HIGH, .pin = 0 },
	{ PIO_CMD_WAIT, .pin = 0, .data = 500  },
	{ PIO_CMD_LOW,  .pin = 0 },
	{ PIO_CMD_WAIT, .pin = 0, .data = 500  },
	{ PIO_CMD_LOOP, .data = 0 },             /* endless */
};

/* Example 2: sample pin 1 after settling */
static const struct pio_cmd read_program[] = {
	{ PIO_CMD_READ, .pin = 1 },
	{ PIO_CMD_LOOP, .data = 0 },             /* repeat forever */
};

int main(void)
{
	int ret;

	if (!device_is_ready(pio)) {
		return -ENODEV;
	}

	pio_aesc_flush(pio);

	/* Load and start the blink program.
	 * write_program handles disable + programReset internally. */
	ret = pio_aesc_write_program(pio, blink_program, ARRAY_SIZE(blink_program));
	if (ret < 0) {
		return ret;
	}

	pio_aesc_enable(pio);


	/* After some time, swap to the read program */
	k_sleep(K_SECONDS(5));

	pio_aesc_flush(pio);   /* wait for current iteration to complete */
	pio_aesc_reset(pio);

      ret = pio_aesc_set_read_delay(pio, 2); /* 2 ticks settling time */
      if (ret < 0) {
          return ret;
      }

      ret = pio_aesc_write_program(pio, read_program, ARRAY_SIZE(read_program));
      if (ret < 0) {
          return ret;
      }

      pio_aesc_enable(pio);

      /* Poll for read results */
      while (1) {
          uint8_t result;

          if (pio_aesc_rx_available(pio) > 0) {
              ret = pio_aesc_read(pio, &result);
              if (ret == 0) {
                  printk("pin 1 = %d\n", result);
              }
          }

          k_sleep(K_MSEC(100));
      }

	return 0;
}
