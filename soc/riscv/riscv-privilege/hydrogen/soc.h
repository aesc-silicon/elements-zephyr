#ifndef __RISCV32_HYDROGEN_SOC_H_
#define __RISCV32_HYDROGEN_SOC_H_

#include <soc_common.h>

/* was previously defined in soc_common.h, but removed later on zephyr upstream */
#define RISCV_MAX_GENERIC_IRQ        12 /* Max Generic Interrupt */

/* Timer configuration */
#define RISCV_MTIME_BASE		DT_REG_ADDR(DT_NODELABEL(mtimer))
#define RISCV_MTIMECMP_BASE		RISCV_MTIME_BASE + 0x2

/* lib-c hooks required RAM defined variables */
#define RISCV_RAM_BASE			DT_REG_ADDR(DT_INST(0, mmio_sram))
#define RISCV_RAM_SIZE			DT_REG_SIZE(DT_INST(0, mmio_sram))

#endif /* __RISCV32_HYDROGEN_SOC_H_ */
