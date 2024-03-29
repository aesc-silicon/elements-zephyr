# SPDX-License-Identifier: Apache-2.0
zephyr_compile_options(-mcpu=${GCC_M_CPU} -mno-sdata)

if(CONFIG_SOC_EMSDP_EM4)
  zephyr_compile_options(-mmpy-option=3  -mno-div-rem)
elseif(CONFIG_SOC_EMSDP_EM6)
  zephyr_compile_options(-mmpy-option=3  -mno-div-rem)
elseif(CONFIG_SOC_EMSDP_EM5D)
  zephyr_compile_options(-mmpy-option=6)
elseif(CONFIG_SOC_EMSDP_EM7D)
  zephyr_compile_options(-mmpy-option=6)
elseif(CONFIG_SOC_EMSDP_EM7D_ESP)
  zephyr_compile_options(-mmpy-option=6)
elseif(CONFIG_SOC_EMSDP_EM9D)
  zephyr_compile_options(-mmpy-option=6)
  zephyr_compile_options_ifdef(CONFIG_FPU -mfpu=fpus_all)
elseif(CONFIG_SOC_EMSDP_EM11D)
  zephyr_compile_options(-mmpy-option=6)
  zephyr_compile_options_ifdef(CONFIG_FPU -mfpu=fpuda_all)
endif()

zephyr_include_directories(.)

set(SOC_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld CACHE INTERNAL "")
