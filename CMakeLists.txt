# Minimum Cmake version required to build this project
cmake_minimum_required(VERSION 3.20)

# Locate the Zephyr RTOS source
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# Name the project
project(MeshFlipper)

list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
include(nanopb)

# Make protobufs/ available as an import root so "meshtastic/foo.proto" resolves.
list(APPEND NANOPB_IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/protobufs)

# Compile mesh.proto and all its transitive dependencies.
zephyr_nanopb_sources(app
    protobufs/meshtastic/mesh.proto
    protobufs/meshtastic/channel.proto
    protobufs/meshtastic/config.proto
    protobufs/meshtastic/device_ui.proto
    protobufs/meshtastic/module_config.proto
    protobufs/meshtastic/atak.proto
    protobufs/meshtastic/portnums.proto
    protobufs/meshtastic/telemetry.proto
    protobufs/meshtastic/xmodem.proto
)

# Generated .pb.h files land in build/protobufs/meshtastic/ but are included as
# "meshtastic/X.pb.h", so build/protobufs/ must be on the include path.
target_include_directories(app PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/protobufs)

# Locate the source code for the application
target_sources(
    app
    PRIVATE
    src/main.c
    src/models/ring_buffer.c
    src/models/mesh_message.c
    src/models/mesh_node.c
    src/hardware/uart_hal.c
    src/comms/uart_comms.c
    src/comms/protobuf_handler.c
    src/tasks/message_processor.c
    src/hardware/button_hal.c
    src/hardware/button.c
    src/ui/ui_button_handler.c
    src/hardware/display_hal.c
    src/hardware/display_hal_weact.c
    src/middleware/display_driver.c
    src/ui/on_screen_keyb.c
    src/ui/display_ui_core.c
    src/ui/display_ui_actions.c
    src/ui/display_ui_keyboard.c
)

target_include_directories(
    app
    PRIVATE
    include
)
