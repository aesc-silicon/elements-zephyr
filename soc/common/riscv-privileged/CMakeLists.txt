# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(.)

zephyr_sources(
  soc_irq.S
  soc_common_irq.c
  vector.S
  )
