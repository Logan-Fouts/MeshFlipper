# Minimum Cmake version required to build this project
cmake_minimum_required(VERSION 3.20)

# Locate the Zephyr RTOS source
find_packages(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# Name the project
project(MeshFlipper)

# Locate teh source code for the application
target_sources(
    ${PROJECT_NAME}
    PRIVATE
    src/main.c
)