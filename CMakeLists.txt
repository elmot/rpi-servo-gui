cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_FETCH_FROM_GIT on)
include(pico_sdk_import.cmake)

project(rpi-servo-gui C CXX ASM)

pico_sdk_init()

add_executable(${CMAKE_PROJECT_NAME}
    main.c
    usb_descriptors.c
    msc_disk.c
    index_htm_embed.S
)

# .incbin in the .S file resolves relative to the assembler's working dir;
# make sure it can find index.htm in the source tree.
set_source_files_properties(index_htm_embed.S PROPERTIES
    COMPILE_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}"
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/index.htm"
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(${CMAKE_PROJECT_NAME}
    pico_stdlib
    tinyusb_device
    tinyusb_board
)

pico_enable_stdio_uart(${CMAKE_PROJECT_NAME} 0)
pico_enable_stdio_usb(${CMAKE_PROJECT_NAME} 0)

