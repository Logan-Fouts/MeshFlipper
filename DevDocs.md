# Dev Docs
Useful dev info and commands

## Commands
### Building
- `west build -p always -b rpi_pico -- -DDTC_OVERLAY_FILE=boards/rpi_pico.overlay -DCONF_FILE=boards/rpi_pico.conf`
## Flashing
- `west flash --runner uf2`