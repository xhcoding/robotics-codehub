# 主机端单元测试：用 mock HAL 在 PC 上跑 stm32::Uart 的逻辑（不参与交叉编译）
# 用法：
#   cmake -S embedded/stm32/tests/host_uart -B build/host_uart
#   cmake --build build/host_uart && ctest --test-dir build/host_uart --output-on-failure
cmake_minimum_required(VERSION 3.22)
project(uart_host_test CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(uart_host_test
  test_uart.cpp
  mock_hal.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../common/peripheral/uart.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../common/peripheral/gpio.cpp
)

target_include_directories(uart_host_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include           # mock stm32f1xx_hal.h 必须在真实 HAL 之前
  ${CMAKE_CURRENT_SOURCE_DIR}/../../common
)

target_compile_options(uart_host_test PRIVATE -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(uart_host_test PRIVATE -fsanitize=address,undefined)

# 自实现格式化器：与主机 libc snprintf 做差分对照（不需要 HAL）
add_executable(fmt_host_test
  fmt_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../common/debug/fmt.cpp
)
target_include_directories(fmt_host_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../common)
target_compile_options(fmt_host_test PRIVATE -Wall -Wextra -Wno-format-truncation -fsanitize=address,undefined)
target_link_options(fmt_host_test PRIVATE -fsanitize=address,undefined)

# 调试打印层：真 dbg.cpp + fmt.cpp 跑在 mock HAL + 真 Uart 上
add_executable(dbg_host_test
  dbg_test.cpp
  mock_hal.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../common/debug/dbg.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../common/debug/fmt.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../common/peripheral/uart.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../common/peripheral/gpio.cpp
)
target_include_directories(dbg_host_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../common
)
target_compile_options(dbg_host_test PRIVATE -Wall -Wextra -fsanitize=address,undefined)
target_link_options(dbg_host_test PRIVATE -fsanitize=address,undefined)

# 差分测试的规模：跑两遍（-O0 抓 UB，-O2 看优化后是否仍一致）
add_executable(fmt_host_test_O2
  fmt_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../common/debug/fmt.cpp
)
target_include_directories(fmt_host_test_O2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../common)
target_compile_options(fmt_host_test_O2 PRIVATE -O2 -Wall -Wextra -Wno-format-truncation)

foreach(_lvl 0 2)
  add_executable(dbg_host_test_L${_lvl}
    dbg_test.cpp
    mock_hal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/debug/dbg.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/debug/fmt.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/peripheral/uart.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/peripheral/gpio.cpp
  )
  target_include_directories(dbg_host_test_L${_lvl} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
  )
  target_compile_definitions(dbg_host_test_L${_lvl} PRIVATE DBG_LEVEL=${_lvl})
  target_compile_options(dbg_host_test_L${_lvl} PRIVATE -Wall -Wextra -fsanitize=address,undefined)
  target_link_options(dbg_host_test_L${_lvl} PRIVATE -fsanitize=address,undefined)
endforeach()

enable_testing()
add_test(NAME uart_host_test COMMAND uart_host_test)
add_test(NAME fmt_host_test COMMAND fmt_host_test)
add_test(NAME fmt_host_test_O2 COMMAND fmt_host_test_O2)
add_test(NAME dbg_host_test COMMAND dbg_host_test)
add_test(NAME dbg_host_test_L0 COMMAND dbg_host_test_L0)
add_test(NAME dbg_host_test_L2 COMMAND dbg_host_test_L2)
