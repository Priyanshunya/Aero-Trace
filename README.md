# AeroTrace: Hyperlocal Pollution Intelligence
> **SEVA First Innovation Challenge 2026 | Team Vision.exe**
> **City & Urban Problems**

[![Hardware](https://img.shields.io/badge/Compute-ESP32--S3-blue)](https://www.espressif.com/)
[![RF](https://img.shields.io/badge/Network-SX1262%20LoRa-orange)](https://www.semtech.com/)
[![Target](https://img.shields.io/badge/Target%20Cost-%E2%82%B98%2C500-brightgreen)]()

## Overview: Curing Data Paralysis
Current government-grade Continuous Ambient Air Quality Monitoring Stations (CAAQMS) have massive spatial blind spots and cannot observe street-level micro-emissions. Meanwhile, uncorrected low-cost sensors suffer from chemical cross-sensitivity and humidity-driven false readings, showing false PM spikes of up to 30-50% during high-humidity winter fog. 

This creates "dispatch paralysis": municipal enforcement teams receive generic heatmaps without verified evidence, making dispatching response teams pure guesswork. 

**AeroTrace** bridges this gap. It turns a local anomaly into a trustworthy, GPS-pinned dispatch lead. By executing an empirical Multiple Linear Regression (MLR) matrix at the edge, the node mathematically filters out weather-driven sensor hallucinations before they trigger an alert. It targets a >95% reduction in humidity-driven false alarms.

## Core Engineering Pillars
Instead of passive data dashboards, our backend outputs a ranked "Next-Best-Check" dispatch queue to direct enforcement teams to exact GPS coordinates based on anomaly persistence and severity.

- **Edge-Triggered SRAM Isolation:** Instead of blindly streaming raw 1Hz data, the ESP32-S3 buffers pre-event data locally in an SRAM circular buffer.
- **Hypothesis-Falsification Engine:** Integrates SPEC CO/NO2 electrochemical gas tracers alongside dust ratios to chemically classify the violation type (e.g., distinguishing an illegal biomass fire from a dusty construction site) before inspectors arrive.
- **Zero-OpEx Private LoRa:** Uses an SX1262 LoRa transceiver operating on the 865-867 MHz (India) band with AES-128/TLS 1.3 security to transmit compressed binary packets, avoiding cellular SIM fees entirely.
- **Continuous Power-Path:** Employs a TI BQ24075 AC-to-battery failover system, piggybacking on municipal AC grids (like streetlights) and seamlessly switching to a 21700 Li-ion battery backup during daytime grid shutoffs.

## Hardware Architecture & BOM (Target: ₹8,500)

| Circuit Schematic | 3D Design |
| :---: | :---: |
| ![Schematic](hardware/Schematic_Diagram.png) | ![AeroTrace 3D Design](hardware/3D_Design_AeroTrace.png) |

- **Compute:** ESP32-S3 (Edge compute & SRAM anomaly buffering)
- **Particulate Sensing:** Plantower PMS5003 (PM1.0/2.5/10)
- **Climate / Humidity Compensation:** Sensirion SHT40 (±0.2% RH)
- **Combustion Gas Tracers:** SPEC CO / NO2 with LMP91000 ultra-low-noise analog front-end (AFE)
- **RF Transceiver:** SX1262 LoRa Module

## Edge Triage Pipeline
Every alert must survive five checks (Capture, Quality Gate, Corroborate, Falsify, Verify) before reaching a field team.

1. **Edge MLR Correction:**
   `PM_corr = β0 + β1*PM_raw + β2*RH + β3*RH² + β4*T + ε`
2. **Incident Priority Index (IPI) Scoring:**
   `|P| = 100 × Q × (w_A*A + w_P*P + w_S*S + w_W*W)`
   *(Where Q = Data-quality, A = Anomaly magnitude, P = Plume agreement, S = Sensor confidence, W = Wind consistency)*
