/*
 * Copyright (c) 2020, Manivannan Sadhasivam <mani@kernel.org>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/arch/arm/cortex_m/scripts/linker.ld>

SECTIONS
     {
#ifdef CONFIG_OPENAMP_RSC_TABLE
        SECTION_PROLOGUE(.resource_table,, SUBALIGN(8))
        {
            KEEP(*(.resource_table*))
        } GROUP_LINK_IN(ROMABLE_REGION)
#endif
     }
