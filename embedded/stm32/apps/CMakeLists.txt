# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

add_stm32_executable(reg_led
    SOURCES reg_led.cpp
    LINK_LIBS stm32_startup
)

add_stm32_executable(hal_led
    SOURCES hal_led.cpp
    LINK_LIBS stm32_common
)
