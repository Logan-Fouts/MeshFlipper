# Minimum Cmake version required to build this project
cmake_minimum_required(VERSION 3.20)

# Locate the Zephyr RTOS source
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# Name the project
project(MeshFlipper)

list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
include(nanopb)

# Locate the source code for the application
target_sources(
    app
    PRIVATE
    src/main.c
    src/models/node.c
    src/models/message.c
)

target_include_directories(
    app
    PRIVATE
    include
)