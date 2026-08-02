cmake_minimum_required(VERSION 3.22)

project(stm32)

include(cmake/STM32Utils.cmake)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug")
endif()

enable_language(C ASM)

add_subdirectory(drivers)

add_subdirectory(startup)

add_subdirectory(apps)
