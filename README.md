# ESP32-Smart-Home-UI-Hub-Universal-IR-Remote
Overview

A centralized smart home control terminal built around an ESP32 microcontroller and a 2.8-inch TFT touch display. This hub unifies control over disparate home appliances by bridging Infrared (IR), Bluetooth Low Energy (BLE), and Wi-Fi protocols into a single, intuitive touch interface.

FEATURES 
Custom GUI: Multi-screen touch interface programmed over a high-speed SPI bus using TFT_eSPI.

Complex HVAC Control: Synthesizes and transmits raw state arrays (Temp, Fan, Swing, Power) for Voltas/Hitachi Air Conditioners using IRac.

Universal TV Control: Replays raw 32-bit Samsung hex codes for standard television operation.

BLE HID Emulation: Acts as a wireless Bluetooth keyboard to navigate streaming devices (e.g., Amazon Fire TV).

IoT Wi-Fi Sync: Connects to local networks to fetch atomic time via NTP for a real-time digital screensaver clock.

Parallel Power Architecture: Runs on a custom-wired 700mAh parallel LiPo battery pack (3.7V) with diode-stepped voltage regulation for stable portable use.

 Hardware Requirements

Microcontroller: ESP32 Development Board

Display: 2.8" TFT LCD Touch Screen (SPI)

Transmitter: 940nm IR LED (with 220Ω resistor)

Power: 2x 350mAh 3.7V LiPo Batteries (Wired in Parallel)

Regulation: 1N4007 Diode (for 0.7V drop to 3.0V safe operating logic)

Charging: TP4056 Lithium Battery Charger Module (1A)

Switch: Physical SPST Toggle Switch

Wiring Schematic (Power Subsystem)

Due to the high current spikes required by IR transmission and the TFT backlight, power is handled via a parallel battery array bypassing standard buck converters for maximum runtime:

Batteries: Wired in parallel (+ to +, - to -) yielding 3.7V @ 700mAh.

Charging: TP4056 connected directly to the parallel battery leads.

Regulation: Battery Positive runs through the physical switch, into the Anode of a 1N4007 diode.

ESP32 & TFT: The Cathode (striped end) of the diode connects to the 3V3 pin of the ESP32 and the VCC/VIN of the TFT, dropping the 4.2V max charge to a safe ~3.5V.

Software Dependencies

Install the following libraries via the Arduino Library Manager:

TFT_eSPI (Configure User_Setup.h for your specific display driver)

IRremoteESP8266 (For AC state synthesis and raw IR transmission)

BleKeyboard (For Fire TV navigation)

Future Improvements

Transitioning from physical switch to ESP32 Deep Sleep architecture using touch-interrupt wakeups.

Adding a dedicated Buck/Boost converter for enhanced battery discharge curve utilization.

Expanding the BLE module to support multi-device pairing.
