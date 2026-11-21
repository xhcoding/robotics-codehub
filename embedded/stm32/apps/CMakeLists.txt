# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

add_stm32_executable(reg_led
    SOURCES reg_led.cpp
    LINK_LIBS stm32_common
)

add_stm32_executable(hal_led
    SOURCES hal_led.cpp
    LINK_LIBS stm32_common
)

add_stm32_executable(key_led
    SOURCES key_led.cpp
    LINK_LIBS stm32_common
)

add_stm32_executable(tim_demo
    SOURCES tim_demo.cpp
    LINK_LIBS stm32_common
)

add_stm32_executable(uart_echo
    SOURCES uart_echo.cpp
    LINK_LIBS stm32_common
)


add_stm32_executable(dbg_demo
    SOURCES dbg_demo.cpp
    LINK_LIBS stm32_common
)
