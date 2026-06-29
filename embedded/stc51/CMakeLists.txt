cmake_minimum_required(VERSION 4.0.0)

set(CMAKE_C_COMPILER sdcc)
set(CMAKE_SYSTEM_NAME Generic)

project(stc51 C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(/usr/share/sdcc/include)

# stcgal 烧录选项
set(STCGAL_PORT "/dev/ttyUSB0" CACHE STRING "Serial port for STC MCU")
set(STCGAL_BAUD "1200" CACHE STRING "Trans baud rate (1200/2400/115200)")
set(STCGAL_HANDSHAKE_BAUD "1200" CACHE STRING "Handshake baud rate (1200/2400/115200)")
set(STCGAL_MODEL "stc89" CACHE STRING "MCU model: stc89, stc12, stc15, stc8, auto")
option(STCGAL_VERBOSE "Enable verbose stcgal output" OFF)

# 最终 stcgal 命令
set(STCGAL_CMD stcgal -p ${STCGAL_PORT} -b ${STCGAL_BAUD} -l ${STCGAL_HANDSHAKE_BAUD} -P ${STCGAL_MODEL})
if (STCGAL_VERBOSE)
  set(STCGAL_CMD "${STCGAL_CMD} -D")
endif()

# 添加 51 target
function(add_51_target TARGET_NAME)
    add_executable(${TARGET_NAME} ${ARGN})

    set(HEX_DIR ${CMAKE_BINARY_DIR}/hex_files)
    file(MAKE_DIRECTORY ${HEX_DIR})
    set(HEX_FILE ${HEX_DIR}/${TARGET_NAME}.hex)

    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND packihx $<TARGET_FILE:${TARGET_NAME}> > ${HEX_FILE}
        COMMENT "Generating ${TARGET_NAME}.hex"
    )

    add_custom_target(flash_${TARGET_NAME}
        DEPENDS ${TARGET_NAME}
        COMMAND ${STCGAL_CMD} ${HEX_FILE}
        COMMENT "Flashing ${HEX_FILE} with stcgal on ${STCGAL_PORT}"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endfunction()

add_51_target(loop loop.c)

add_51_target(first_led led/first_led.c)
add_51_target(blink_led led/blink_led.c)
add_51_target(chaser_led led/chaser_led.c)

add_51_target(check_key key/check_key.c)
add_51_target(number_key key/number_key.c)

add_51_target(single_digital_tube digital_tube/single_digital_tube.c)
