# MeshFlipper

This is the microcontroller side of the implementation for the MeshFlipper software. It aims to create a flip phone(or similar)-style Meshtastic device with a microcontroller handling the UI/UX and a different Meshtastic board for LoRa communication.

## What It Is

I intend to build a device that combines the simplicity and functionality of a traditional flip phone or another similar bare bones phone with the advanced capabilities of a Meshtastic network. The microcontroller will manage the user interface, while another Meshtastic board will handle the LoRa communication and data processing.

## Stack

### Hardware
- **Raspberry Pi Pico (UI/UX)**
- **Heltec v3 (LoRa)**
- **I2C e-Ink Display**
- **Joystick and/or other ui buttons** (tbd)
- **Battery and Other Extraneous Components**

## Goals

- **Microcontroller UI/UX**: Develop a user-friendly interface using the Raspberry Pi Pico.
- **LoRa Communication**: Implement efficient communication between the microcontroller and the Heltec v3 board using UART.
- **Advanced Features**: Incorporate additional features like addint in other sensors and fleshing out the hardware.

## Development Progress

Feel free to explore the repository for the latest updates. If you find this project interesting or want to contribute, feel free to star it!
I am just using this as an excuse to learn Zephyr RTOS :)