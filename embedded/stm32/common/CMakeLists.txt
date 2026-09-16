add_library(stm32_common)

target_include_directories(stm32_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(stm32_common PUBLIC stm32_startup)

target_sources(stm32_common PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/system/system.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/peripheral/gpio.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/component/led.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/component/key.cpp
)



