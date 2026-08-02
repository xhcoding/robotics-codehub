add_library(stm32_drivers INTERFACE)

target_include_directories(stm32_drivers INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS/Include
  ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS/Device/ST/STM32F1xx/Include
)

target_compile_definitions(stm32_drivers INTERFACE
	STM32F103xB
    $<$<CONFIG:Debug>:DEBUG>
)
