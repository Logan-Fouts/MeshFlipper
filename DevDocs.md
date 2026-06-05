# Dev Docs
Useful dev info and commands

## Commands
### Building
- `west build -p always -b rpi_pico -- -DDTC_OVERLAY_FILE=boards/rpi_pico.overlay -DCONF_FILE=boards/rpi_pico.conf`
## Flashing
- `west flash --runner uf2`


## Meshtastic protobuf header
The 4 byte header is constructed to both provide framing and to not look like 'normal' 7 bit ASCII.

    Byte 0: START1 (0x94)
    Byte 1: START2 (0xc3)
    Byte 2: MSB of protobuf length
    Byte 3: LSB of protobuf length

