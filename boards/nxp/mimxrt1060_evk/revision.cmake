if (NOT DEFINED BOARD_REVISION)
  set(BOARD_REVISION "qspi")
else ()
  if (NOT (BOARD_REVISION STREQUAL "hyperflash") AND NOT (BOARD_REVISION STREQUAL "qspi"))
    message(FATAL_ERROR "Invalid board revision, ${BOARD_REVISION}, valid revisions are: hyperflash, qspi")
  elseif (BOARD_REVISION STREQUAL "hyperflash" AND CONFIG_BOARD_MIMXRT1060_EVKB)
    message(FATAL_ERROR "hyperflash not supported on RT1060 EVKB")
  endif()
endif()
