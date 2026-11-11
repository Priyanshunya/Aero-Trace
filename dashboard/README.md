# AeroTrace Local Dashboard

This directory contains the frontend for bench-testing the AeroTrace edge node. It is a single-page HTML dashboard built to visualize our live BLE telemetry stream locally before we scale up to a full cloud backend.

### Architecture & Tech Stack
* **Frontend:** Pure HTML/CSS/JS (Vanilla) with a lightweight SaaS-style grid.
* **Connectivity:** Native Web Bluetooth API hooking into the ESP32's Nordic UART GATT service.
* **Data Parsing:** Assembles fragmented BLE byte chunks into valid JSON objects on the fly.
* **Visualization:** Chart.js (via CDN) handling the rolling time-series graphs (capped at 100 data points to prevent browser memory bloat).

### Telemetry & Features
* **Live KPI Grid:** Real-time metrics for PM2.5, PM10, Temp, RH, and raw MQ-7/MQ-135 ADC voltages.
* **Sensor Array Charts:** Three independent rolling graphs tracking Particulate Scatter, Climate Data, and Analog Gas Voltages at ~2.5 Hz.
* **Diagnostics Terminal:** A raw JSON payload inspector and packet sequence tracker for easy hardware debugging without needing the Arduino Serial Monitor.
