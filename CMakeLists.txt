cmake_minimum_required(VERSION 3.21)

project(paradox LANGUAGES CXX C ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # Generates compile_commands.json for IDEs (clangd/ccls)

# Define the hardware board macro
add_compile_definitions(ALCHEMY_BOARD_V2 CORE_CM7 STM32H750IB ARM_MATH_CM7)

# Include directories from the Alchemy SDK
include_directories(
    src
    lib/alchemy-sdk/framework/include
    lib/alchemy-sdk/hardware/include
    lib/alchemy-sdk/hardware/alchemy-lab/v2/include
)

# Include directories from libDaisy (Required for IDE symbol resolution)
include_directories(
    lib/libDaisy/src
    lib/libDaisy/src/sys
    lib/libDaisy/Drivers/CMSIS_5/CMSIS/Core/Include
    lib/libDaisy/Drivers/CMSIS-DSP/Include
    lib/libDaisy/Drivers/CMSIS-Device/ST/STM32H7xx/Include
    lib/libDaisy/Drivers/STM32H7xx_HAL_Driver/Inc
    lib/libDaisy/Middlewares/ST/STM32_USB_Device_Library/Core/Inc
    lib/libDaisy/Middlewares/ST/STM32_USB_Host_Library/Core/Inc
    lib/libDaisy/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Inc
    lib/libDaisy/Middlewares/ST/STM32_USB_Host_Library/Class/MIDI/Inc
    lib/libDaisy/core
    lib/libDaisy/Middlewares/Third_Party/FatFs/src
)

# Gather source files
file(GLOB_RECURSE ALCHEMY_SDK_SRCS "lib/alchemy-sdk/framework/src/*.cpp")
file(GLOB_RECURSE ALCHEMY_HW_SRCS "lib/alchemy-sdk/hardware/alchemy-lab/v2/src/*.cpp")

# Define the executable (useful for the IDE to build AST)
add_executable(paradox
    src/paradox.cpp
    src/paradox_dsp.cpp
    ${ALCHEMY_SDK_SRCS}
    ${ALCHEMY_HW_SRCS}
)

# If we are actually building via CMake (instead of make), link libDaisy
if(CMAKE_CROSSCOMPILING)
    add_subdirectory(lib/libDaisy EXCLUDE_FROM_ALL)
    target_link_libraries(paradox PRIVATE daisy)
endif()
