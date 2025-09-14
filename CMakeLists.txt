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
    PUBLIC                      # <- PUBLIC so dependents also “see” headers if needed
        neopixel_ring
        pico_stdlib
        hardware_pio
)

target_include_directories(rotary PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/drivers/rotary
    ${CMAKE_CURRENT_LIST_DIR}/drivers/ws2812     # (still fine to keep)
    # (we could omit drivers/ring here because PUBLIC link to neopixel_ring already exports it)
)

# ---------- ring driver ----------------------------------------------------
add_library(neopixel_ring STATIC
    drivers/ring/NeopixelRing.cpp
)

target_link_libraries(neopixel_ring
    PUBLIC
        ws2812
)

target_include_directories(neopixel_ring
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/drivers/ring
        ${CMAKE_CURRENT_LIST_DIR}/include
)

# Make rotary inherit ring headers/libs
target_link_libraries(rotary PUBLIC neopixel_ring)

# ---------- ws2812 driver --------------------------------------------------
add_library(ws2812 STATIC
    drivers/ws2812/Ws2812.cpp
    drivers/ws2812/ws2812.pio
)
pico_generate_pio_header(ws2812
    ${CMAKE_CURRENT_LIST_DIR}/drivers/ws2812/ws2812.pio
)

target_link_libraries(ws2812
    pico_stdlib
    hardware_pio
    hardware_clocks
)

# Export the actual ws2812 header location so anything including "Ws2812.hpp" works
target_include_directories(ws2812
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/drivers/ws2812
        ${CMAKE_CURRENT_LIST_DIR}/include
)

# ---------- sevenseg driver ------------------------------------------------
add_library(sevenseg STATIC
    drivers/sevenseg/SevenSeg.cpp
    drivers/sevenseg/SevenSegLayout.cpp   
)

target_link_libraries(sevenseg
    PUBLIC
        ws2812
)

target_include_directories(sevenseg
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/drivers/sevenseg
        ${CMAKE_CURRENT_LIST_DIR}/include
)

# ---------- HX711 driver ---------------------------------------------------
add_library(hx711 STATIC
    drivers/hx711/hx711.cpp         
    drivers/hx711/config_store.cpp 
)

pico_generate_pio_header(hx711
    ${CMAKE_CURRENT_LIST_DIR}/drivers/hx711/hx711_reader.pio
)

target_link_libraries(hx711
    PUBLIC
        pico_stdlib
        hardware_pio
        hardware_gpio
        hardware_flash
)

target_include_directories(hx711
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/drivers/hx711
        ${CMAKE_CURRENT_LIST_DIR}/include   # if you expose public headers there
)

# ---------- Buzzer driver --------------------------------------------------
add_library(buzzer STATIC
    drivers/buzzer/Buzzer.cpp
)

target_link_libraries(buzzer
    PUBLIC
    pico_stdlib
    hardware_pwm
    hardware_clocks
    )

# Add the standard include files to the build
target_include_directories(buzzer PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/drivers/buzzer        
)

# ---------- Lcd1602I2C driver ----------------------------------------------
add_library(lcd1602_i2c STATIC
    drivers/lcd/Lcd1602I2C.cpp
    # drivers/lcd/Lcd1602I2C.hpp
)
target_include_directories(lcd1602_i2c PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/drivers/lcd
)
target_link_libraries(lcd1602_i2c
    pico_stdlib
    hardware_i2c
)

# ---------- executable -----------------------------------------------------
add_executable(NewKorndispenser
    app/main.cpp
)

pico_set_program_name   (NewKorndispenser "NewKorndispenser")
pico_set_program_version(NewKorndispenser "0.1")

target_link_libraries(NewKorndispenser
    buzzer
    rotary
    neopixel_ring 
    sevenseg
    ws2812
    hx711
    lcd1602_i2c
    pico_stdlib
)

pico_enable_stdio_usb(NewKorndispenser 1)
pico_add_extra_outputs(NewKorndispenser)