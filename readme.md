# StaticTesterDatalogger

A WiFi-enabled datalogger for load cell measurements using ESP32, HX711, and SD card storage.  
Provides a web interface for calibration, recording, and downloading measurement files.

## Features

- **Load Cell Support:** Uses HX711 ADC for precise force measurements.
- **Web Interface:** Start/stop logging, tare, calibrate, and download recordings from any browser.
- **SD Card Storage:** Saves measurements as CSV files in `/recordings` folder.
- **WiFi Manager:** Easily configure WiFi credentials via captive portal.
- **NTP Time:** Filenames use network time for easy identification.
- **Calibration:** Two-step calibration via web interface.
- **File Download:** Download CSV recordings directly from the web UI.

## Hardware

- ESP32 board
- HX711 load cell amplifier
- Load cell sensor
- SD card module (SPI)
- Optional: External power supply for load cell

## Usage

1. **Wiring:**  
   - Connect HX711 to ESP32 pins (default: DOUT=17, SCK=16).
   - Connect SD card module to SPI pins (default: SCK=15, MISO=32, MOSI=33, CS=-1).

2. **Flash the firmware:**  
   - Open `StaticFile.ino` in Arduino IDE or PlatformIO.
   - Install required libraries:  
     - `ESPAsyncWebServer`, `WiFiManager`, `HX711_ADC`, `NTPClient`, `SD`, `SPI`
   - Upload to ESP32.

3. **Connect to WiFi:**  
   - On first boot, ESP32 creates a WiFi access point.
   - Connect and configure WiFi via captive portal.

4. **Web Interface:**  
   - Access the device via its IP address (or `esp32dl.local` if mDNS is supported).
   - Use the interface to tare, calibrate, start/stop logging, and download files.

5. **Recording:**  
   - Press "Start" to begin logging.
   - Press "Stop" to end and save data to SD card.
   - Download CSV files from the "Recordings" section.

## File Structure

- `StaticFile.ino` — Main firmware for ESP32
- `HTML/index.html` — Main web interface
- `HTML/calibration.html` — Calibration page

## API Endpoints

- `/tare` (POST): Tare the load cell
- `/calibrate` (GET): Calibrate with known mass (`?mass=VALUE`)
- `/start` (POST): Start logging
- `/stop` (POST): Stop logging and save data
- `/list?dir=/recordings` (GET): List recordings as JSON
- `/recordings/<filename>` (GET): Download CSV file

## Example CSV Output

```
timestamp,value
12345,0.123456
12456,0.234567
...
```

## License

Apache 2.0
Copyright 2025 Ferran Espigares Garcia 

---

**Note:**  
For best results, use a genuine ESP32, quality SD card, and ensure stable power for the load cell and HX711.

Files for a 3d rocket test stand are included in "3d-stand-files"
