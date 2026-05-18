# AURA-Kruecke

ESP32-S3-basiertes Variometer-Prototyping auf dem LilyGo T5 E-Paper S3 Pro.

## Hardware

- **Board:** LilyGo T5 E-Paper S3 Pro (ESP32-S3, 16 MB Flash, OPI PSRAM)
- **Sensoren (extern via I2C):** 2x BMP581, LSM6DSO32, BNO085, SHT40
- **Onboard:** BQ25896 Charger, BQ27220 Fuel Gauge, PCF85063 RTC, 4.7" E-Paper

## Setup

1. [PlatformIO](https://platformio.org/) installieren
2. `pio run` — kompiliert und zieht Dependencies
3. `pio run -t upload` — flasht auf das Board
4. `pio device monitor` — Serial Monitor @ 115200 Baud
