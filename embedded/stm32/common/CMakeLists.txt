add_library(stm32_common)

target_include_directories(stm32_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(stm32_common PUBLIC stm32_startup)

target_sources(stm32_common PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/system/system.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/peripheral/gpio.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/peripheral/tim.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/peripheral/uart.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/component/led.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/component/key.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/dbg/fmt.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/dbg/dbg.cpp
)

# 调试打印等级（0=彻底编译掉 1=E 2=W 3=I 4=V）；Debug 默认全开，Release 默认只留 E/W
if(NOT DEFINED STM32_DBG_LEVEL)
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(STM32_DBG_LEVEL 2)
  else()
    set(STM32_DBG_LEVEL 4)
  endif()
endif()
target_compile_definitions(stm32_common PUBLIC DBG_LEVEL=${STM32_DBG_LEVEL})

# 不需要浮点格式时可用 STM32_DBG_NO_FLOAT=ON 省掉软浮点格式化（约 2.5 KB）
option(STM32_DBG_NO_FLOAT "调试打印去掉 %f 支持（省 ~2.5KB flash）" OFF)
if(STM32_DBG_NO_FLOAT)
  target_compile_definitions(stm32_common PUBLIC STM32_DBG_NO_FLOAT)
endif()



