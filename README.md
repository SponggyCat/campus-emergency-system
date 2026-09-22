An offline, multi-sensor campus safety system that detects earthquakes, fires, and flood/gate status across multiple locations, 
cross-validates sensor readings to reduce false alarms, and displays everything on a live dashboard hosted directly on an ESP32 — built for Electives 2.

All of this feeds into a central ESP32, which drives an LCD display and buzzer for local alerts, and hosts a live web dashboard — served directly from 
the device itself over its own Wi-Fi access point, with no internet or external server required.

## Why offline/radio instead of Firebase
This project originally used Firebase to sync data between devices. It was switched to nRF24L01 radio modules for device-to-device communication instead, so the system works fully offline
— appropriate for an emergency/safety system that shouldn't depend on internet access being available during a disaster.

## How it works
1. A local SW420 vibration sensor and remote radio-connected quake sensor both report earthquake activity; the system cross-validates both within a 5-second window before confirming an earthquake.
2. Remote nodes report fire status for two separate rooms over nRF24L01 radio.
3. A separate device reports gate and creek/flood status over a wired UART serial connection, using a simple pipe-delimited message format.
4. The central ESP32 aggregates all of this, prioritizes alerts (confirmed earthquake > fire > other), and drives an LCD + buzzer for immediate local alerting.
5. A dashboard, stored on the ESP32 via LittleFS, polls a `/data` JSON endpoint and displays live status — including an animated 3D-style campus map with per-building fire/quake visual indicators.
6. The ESP32 runs as its own Wi-Fi access point, so the dashboard is accessible to anyone nearby without needing existing network infrastructure.

## Tech used
- ESP32 (Wi-Fi AP + web server)
- nRF24L01 radio modules (wireless device-to-device communication)
- SW420 vibration sensor
- UART serial communication (inter-device)
- LittleFS (on-device file storage for the web dashboard)
- ArduinoJson (data serialization for the `/data` API)
- I2C LCD display
- HTML/CSS/JavaScript (live-polling dashboard with animated visualization)

## Why this project
Originally an individual earthquake sensor project, then expanded by course assignment to integrate flood and fire sensors built by classmates into one unified, offline-capable emergency monitoring system.

## Why this project
Originally an individual earthquake sensor project, then expanded by course assignment to integrate flood and fire sensors built by classmates into one unified, offline-capable emergency monitoring system.
