# ESP32 Multi-Location Air Quality Panel

This is an ESP32 based eink display showing weather and air quality (PM2.5) data for multiple locations using 
the [ELECROW CrowPanel ESP32 E-Paper HMI 2.13-inch Display](https://www.elecrow.com/wiki/CrowPanel_ESP32_E-Paper_HMI_2.13-inch_Display.html).

It should also work on any other ESP32 boards with an e-ink display of resolution at least 250x122, up to updating pin numbers and possibly updating the driver if not SSD1680-based.

Screenshot:

![Board](https://raw.githubusercontent.com/roos-j/multi-location-air-quality-panel-esp32-2.13in/screenshots/assets/board.jpg)

### Features

- Monochrome 250x122 display, ESP32S3 with WiFi
- Weather data from [open-meteo](https://open-meteo.com/) API
- PM2.5 data from [PurpleAir](https://www2.purpleair.com/) sensors
- Time series data stored in flash
- Web portal for config and time series management
- Display stays on without power, only requires power to update data
- Custom SSD1680 display driver
- Open source font (Noto Sans Mono Regular)

### Parts

- [ELECROW CrowPanel ESP32 E-Paper HMI 2.13-inch Display](https://www.elecrow.com/wiki/CrowPanel_ESP32_E-Paper_HMI_2.13-inch_Display.html) (full board, no wiring needed)
- USB-C cable for power and connection to PC

### Setup instructions

- Download [Arduino IDE](https://www.arduino.cc/en/software/)
- Install ESP32 libraries and ArduinoJson
- Download/clone this repository and open in Arduino IDE
- (Optional) Configure default locations and PurpleAir sensors in `preset.h` (get a [PurpleAir API key](https://community.purpleair.com/t/about-the-purpleair-api/7145))
- Connect board via USB, select port and select `ESP32S3 Dev Module` in Arduino IDE, then compile/upload
- Change settings or view/download time series data via web portal available after pressing `MENU`

### AI Usage

gpt-5.5 and codex/gpt-5.4-mini-xhigh were used to generate font bitmaps, weather icons, a lot of the utility code, and most of the web portal.