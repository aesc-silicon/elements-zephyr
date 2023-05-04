#ifndef __RISCV32_ELEMENTS_VEXRISCV_SOC_H_
#define __RISCV32_ELEMENTS_VEXRISCV_SOC_H_

#include <soc_common.h>

/* lib-c hooks required RAM defined variables */
#define RISCV_RAM_BASE			DT_REG_ADDR(DT_INST(0, mmio_sram))
#define RISCV_RAM_SIZE			DT_REG_SIZE(DT_INST(0, mmio_sram))

#endif /* __RISCV32_ELEMENTS_VEXRISCV_SOC_H_ */
