add_library(stm32_drivers)

target_include_directories(stm32_drivers PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS/Include
  ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS/Device/ST/STM32F1xx/Include
  # hal
  ${CMAKE_CURRENT_SOURCE_DIR}/STM32F1xx_HAL_Driver/Inc/Legacy
  ${CMAKE_CURRENT_SOURCE_DIR}/STM32F1xx_HAL_Driver/Inc
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(stm32_drivers PUBLIC
  STM32F103xB
  $<$<CONFIG:Debug>:DEBUG>
)

target_sources(stm32_drivers PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio_ex.c
  ${CMAKE_CURRENT_SOURCE_DIR}/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal.c
  ${CMAKE_CURRENT_SOURCE_DIR}/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc.c
  ${CMAKE_CURRENT_SOURCE_DIR}/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc_ex.c
  ${CMAKE_CURRENT_SOURCE_DIR}/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio.c
  ${CMAKE_CURRENT_SOURCE_DIR}/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_dma.c
  ${CMAKE_CURRENT_SOURCE_DIR}/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_cortex.c
  ${CMAKE_CURRENT_SOURCE_DIR}/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_pwr.c
  ${CMAKE_CURRENT_SOURCE_DIR}/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash.c
  ${CMAKE_CURRENT_SOURCE_DIR}/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash_ex.c
  ${CMAKE_CURRENT_SOURCE_DIR}/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_exti.c
)
