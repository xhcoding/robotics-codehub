add_library(stm32_startup)
target_sources(stm32_startup PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/startup_stm32f103xb.s
  ${CMAKE_CURRENT_SOURCE_DIR}/system_stm32f1xx.c
)

target_link_options(stm32_startup PUBLIC
  -T${CMAKE_CURRENT_SOURCE_DIR}/STM32F103xx_FLASH.ld
)

target_link_libraries(stm32_startup PUBLIC
  stm32_drivers
)
