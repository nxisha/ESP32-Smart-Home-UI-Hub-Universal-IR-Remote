ESP32 Smart Home UI Hub & Universal IR/BLE Remote


A physical demonstration of the Smart Hub navigating Fire TV via BLE and operating Voltas/Hitachi HVAC units via synthesized IR pulses.

Architecture Overview

A centralized smart home control terminal built around an ESP32-WROOM-32 and a 2.8-inch TFT SPI touch display. This hub unifies control over disparate home appliances by bridging Infrared (IR), Bluetooth Low Energy (BLE), and Wi-Fi protocols into a single concurrent interface.

It actively operates a dual-role BLE architecture—acting as a Server (HID Keyboard) for streaming devices, and a Client for reverse-engineered smart lighting control, bypassing proprietary commercial hubs.

Hardware Stack

MCU: ESP32-WROOM-32 Development Board (Tensilica Xtensa Dual-Core 32-bit LX6)

Display Interface: 2.8" TFT LCD Touch Screen (ILI9341 Driver, XPT2046 Touch Controller)

IR Subsystem: 940nm Infrared LED with current-limiting 220Ω series resistor

Power Architecture:

2x 350mAh 3.7V LiPo Batteries (Parallel configuration for 700mAh capacity)

TP4056 Lithium Battery Charger IC (1A regulated input)

1N4007 Diode (Voltage step-down regulation)

GPIO Pinout & Wiring

Peripheral / Component

ESP32 GPIO

Notes / Alternate Function

TFT_MOSI

GPIO 23

VSPI MOSI

TFT_MISO

GPIO 19

VSPI MISO

TFT_SCLK

GPIO 18

VSPI CLK

TFT_CS

GPIO 15

Chip Select

TFT_DC

GPIO 2

Data/Command

TFT_RST

GPIO 4

Hardware Reset

TOUCH_CS

GPIO 21

Touch Controller CS

IR_TX

GPIO 12

PWM Carrier Generation (38kHz)

Power Subsystem & Diode Regulation

Due to the high transient current spikes required by active IR transmission and the TFT backlight, power is handled via a direct parallel battery array, intentionally bypassing switching buck converters to maximize discharge utilization:

Cell Configuration: Wired in parallel yielding 3.7V nominal @ 700mAh.

Voltage Stepping: Battery positive is routed through a physical SPST switch and into the Anode of a 1N4007 diode.

Logic Leveling: The Cathode drops the 4.2V peak charge by ~0.7V (Forward Voltage Drop), delivering a safe ~3.5V to the ESP32 3V3 pin and TFT VIN, operating safely within absolute maximum ratings without converter switching noise.

Proof of Hardware

View KiCad Schematic / Wiring Diagram

View Logic Analyzer Trace (38kHz IR Carrier)

Build & Flash Instructions

1. IDE Configuration

Environment: Arduino IDE 2.x or PlatformIO

Board: ESP32 Dev Module

Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS) (Required due to BLE + TFT library overhead).

2. Dependencies

TFT_eSPI by Bodmer (Must configure User_Setup.h for ILI9341 and assigned VSPI pins).

IRremoteESP8266 by crankyoldgit.

BleKeyboard by T-vK.

3. Syska BLE Configuration

To interface directly with the Syska Smart LED without the manufacturer app, configure the target MAC address in ESP32_Smart_Remote.ino:

// Update to target Syska Bulb MAC Address
BLEAddress bulbAddress("11:22:33:44:55:66"); 


4. Compilation
