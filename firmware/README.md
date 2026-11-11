# AeroTrace Edge Firmware

This directory contains the ESP32 firmware for the AeroTrace prototype.

Currently running `v1_aerotrace` for bench testing. This build reads the PMS5003, SHT10, and analog MQ sensors, and streams the telemetry as JSON over Bluetooth Low Energy (BLE) to test the hardware against a local web dashboard.

### Implementation Notes
* **Memory Management:** Used `snprintf` with a fixed `char` buffer to build the JSON payloads instead of standard Arduino `String` concatenation. This prevents heap fragmentation and keeps the ESP32 stable during continuous uptime.
* **LoRa Standby:** The SX1278 is physically wired to the SPI bus, but its NSS (Chip Select) pin is pulled HIGH in `setup()`. This forces the module to sleep and prevents it from dumping noise onto the analog lines while the BLE stack is running.
* **Non-Blocking:** Sensor polling relies on `millis()` timers. No `delay()` calls are used, ensuring the hardware serial buffer never overflows and drops PMS5003 particulate frames.

### How to Flash
1. Open `v1_aerotrace/v1_aerotrace.ino` in the Arduino IDE.
2. Select **ESP32 Dev Module** from the boards menu.
3. Install the **SHT1x** library via the Arduino Library Manager. (BLE and HardwareSerial are already included in the ESP32 core).
4. Compile and upload.
5. Open Serial Monitor (115200 baud) to verify sensor initialization and the JSON stream.
