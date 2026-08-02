# 函数：创建一个 STM32 可执行目标并自动添加 OpenOCD 烧录目标
# 参数：
#   TARGET_NAME   - 目标名称（如 reg_led）
#   SOURCES       - 源文件列表（可以有多个）
#   LINK_LIBS     - 需要链接的库列表（将使用 PRIVATE 链接）
#   [CHIP_CFG]    - OpenOCD 芯片配置（默认 stm32f1x）
#   [INTERFACE_CFG]- OpenOCD 接口配置（默认 stlink）
function(add_stm32_executable TARGET_NAME)
    # 解析命名参数
    set(options "")
    set(oneValueArgs CHIP_CFG INTERFACE_CFG FLASH_ADDR)
    set(multiValueArgs SOURCES LINK_LIBS)
    cmake_parse_arguments(STM32_ARG
        "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN}
    )

    # 检查必需的 SOURCES 参数
    if(NOT STM32_ARG_SOURCES)
        message(FATAL_ERROR "add_stm32_executable: SOURCES must be provided for target ${TARGET_NAME}")
    endif()

    # ----- 1. 创建可执行目标 -----
    add_executable(${TARGET_NAME} ${STM32_ARG_SOURCES})

    # ----- 2. 链接库（使用 PRIVATE）-----
    if(STM32_ARG_LINK_LIBS)
        target_link_libraries(${TARGET_NAME} PRIVATE ${STM32_ARG_LINK_LIBS})
    endif()

    # ----- 3. 添加烧录目标（调用之前定义的 add_flash_target）-----
    # 设置默认值
    if(NOT STM32_ARG_CHIP_CFG)
        set(STM32_ARG_CHIP_CFG "stm32f1x")
    endif()
    if(NOT STM32_ARG_INTERFACE_CFG)
        set(STM32_ARG_INTERFACE_CFG "stlink")
    endif()

    # 确保 openocd 存在，若不存在则给出警告但不影响编译
    find_program(OPENOCD_EXE openocd)
    if(OPENOCD_EXE)
        add_custom_target(flash_${TARGET_NAME}
            COMMAND ${OPENOCD_EXE}
                -f interface/${STM32_ARG_INTERFACE_CFG}.cfg
                -f target/${STM32_ARG_CHIP_CFG}.cfg
                -c "program $<TARGET_FILE:${TARGET_NAME}> reset exit"
            COMMENT "Flashing $<TARGET_FILE:${TARGET_NAME}> with OpenOCD"
            DEPENDS ${TARGET_NAME}
        )
        # 该目标不会因 make all 而被构建
        set_target_properties(flash_${TARGET_NAME} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    else()
        message(WARNING "OpenOCD not found, flash target '${TARGET_NAME}_flash' will not be created")
    endif()
endfunction()
