# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(NewKorndispenser C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# ---------- rotary driver -------------------------------------------------
add_library(rotary STATIC
    drivers/rotary/Rotary_Button.cpp
    drivers/rotary/quadrature_encoder.pio
)
pico_generate_pio_header(rotary
        ${CMAKE_CURRENT_LIST_DIR}/drivers/rotary/quadrature_encoder.pio)

target_link_libraries(rotary
    pico_stdlib
    hardware_pio
)

target_include_directories(rotary PUBLIC
    include
    drivers/rotary                    
    drivers/ws2812            
)

# ---------- ws2812 driver --------------------------------------------------
add_library(ws2812 STATIC
    drivers/ws2812/Ws2812.cpp
    drivers/ws2812/ws2812.pio
)
pico_generate_pio_header(ws2812
        ${CMAKE_CURRENT_LIST_DIR}/drivers/ws2812/ws2812.pio)
target_link_libraries(ws2812 pico_stdlib hardware_pio hardware_clocks)
target_include_directories(ws2812 PUBLIC include)

# ---------- executable -----------------------------------------------------
add_executable(NewKorndispenser
    app/main.cpp
)

pico_set_program_name   (NewKorndispenser "NewKorndispenser")
pico_set_program_version(NewKorndispenser "0.1")

target_link_libraries(NewKorndispenser
    rotary
    ws2812
    pico_stdlib
)

pico_enable_stdio_usb(NewKorndispenser 1)
pico_add_extra_outputs(NewKorndispenser)