# 🥚 Egg IncuBuddy - ESP32 Incubator Monitor

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A comprehensive IoT solution for monitoring temperature and humidity in egg incubators using an ESP32 microcontroller. This project provides real-time monitoring, data logging, and visualization through a responsive web interface.

![Egg IncuBuddy Interface](https://github.com/Grey-Lancaster/IncuBuddy/blob/main/docs/images/inc.png))

## 📋 Features

- 🌡️ Real-time temperature and humidity monitoring using DHT22 sensor
- ⏱️ Incubation timer with persistent storage across power cycles
- 📊 Interactive graphs with multiple time range views (24 hours, 7 days, all data)
- 📈 Statistical summaries of temperature and humidity data
- 🌗 Dark mode interface
- 📱 Responsive design for desktop and mobile devices
- 📶 Easy WiFi setup using WiFiManager
- 🔄 Over-the-Air (OTA) firmware updates
- 💾 Data logging to SPIFFS file system
- 📤 Data export functionality
- 🐣 Start button for beginning a new batch of eggs
- 🕰️ Input option for eggs already incubating
- ⏰ Custom start time setting for eggs already in incubation
- 🔃 Device restart option via web interface

## 🛠️ Hardware Requirements

- ESP32 development board (Recommended: ESP32-WROOM)
- DHT22 temperature and humidity sensor
- Micro USB cable for power and initial programming
- Optional: 3D printed case

## 📚 Libraries Used

- [Arduino Core for ESP32](https://github.com/espressif/arduino-esp32)
- [WiFiManager](https://github.com/tzapu/WiFiManager)
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)
- [AsyncTCP](https://github.com/me-no-dev/AsyncTCP)
- [DHT sensor library](https://github.com/adafruit/DHT-sensor-library)
- [NTPClient](https://github.com/arduino-libraries/NTPClient)
- [ArduinoJson](https://arduinojson.org/)
- [ElegantOTA](https://github.com/ayushsharma82/ElegantOTA)

## ⚡ Circuit Configuration

Connect the DHT22 sensor to your ESP32 as follows:

- DHT22 VCC → ESP32 3.3V
- DHT22 DATA → ESP32 GPIO21 (configurable in code)
- DHT22 GND → ESP32 GND

## 🚀 Installation and Setup

### Prerequisites

1. Install the [Arduino IDE](https://www.arduino.cc/en/software)
2. Add ESP32 board support to Arduino IDE:
   - Open Arduino IDE
   - Go to File → Preferences
   - Add `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json` to the "Additional Board Manager URLs" field
   - Go to Tools → Board → Boards Manager
   - Search for ESP32 and install

### Install Required Libraries

In the Arduino IDE, go to Tools → Manage Libraries and install:

1. WiFiManager by tzapu
2. AsyncTCP by me-no-dev
3. ESPAsyncWebServer by me-no-dev
4. DHT sensor library by Adafruit
5. NTPClient by Arduino
6. ArduinoJson by Benoit Blanchon
7. SPIFFS by ESP32
8. ElegantOTA by Ayush Sharma

# ESP32 EggTimer Development Guide

## Development Environment Options

### Option 1: Arduino IDE
1. Clone this repository or download the source code
2. Open the `.ino` file in Arduino IDE
3. Select your ESP32 board from Tools → Board
4. Select the correct COM port from Tools → Port
5. **Upload SPIFFS Data Files:**
   - Go to Tools → ESP32 Sketch Data Upload
   - This will upload the contents of the `/data` folder (including `index.html` and `upload.html`) to the ESP32's SPIFFS filesystem
   - Wait for the upload to complete before proceeding
6. Click the Upload button to upload the main sketch

### Option 2: VS Code with PlatformIO
PlatformIO provides a more powerful development environment with advanced features like code completion, debugging, and multi-project management.

1. Install VS Code
2. Install the PlatformIO extension from the VS Code marketplace
3. Clone this repository or download the source code

7. **Upload SPIFFS Data Files:**
   - Use the PlatformIO sidebar
   - Click on "Build Filesystem Image"
   - Click on "Upload Filesystem Image" or use the command palette (Ctrl+Shift+P) and search for "PlatformIO: Upload Filesystem Image"
   - This uploads the `/data` folder contents to SPIFFS
8. Use the PlatformIO sidebar to build and upload the project
9. Monitor the device using the PlatformIO Serial Monitor

## SPIFFS Data Folder Structure
Ensure your project has a `/data` folder in the root directory containing:
```
/data/
├── index.html
├── upload.html
└── [any other web assets]
```

## First-Time Setup
1. Power on your ESP32
2. Connect to the "EggTimer-Setup" WiFi network that appears
3. Follow the captive portal instructions to configure your WiFi credentials
4. The device will connect to your WiFi network and display its IP address on the Serial Monitor
5. You need to reboot the device to start the web service
6. Access the web interface by entering the IP address in your web browser
7. You can also access the device via mDNS at http://eggtimer.local/ if your system supports mDNS

## Important Notes
- **SPIFFS Upload Required:** Since `index.html` and `upload.html` have been moved to SPIFFS to save program space, you must upload the filesystem data before the web interface will work properly
- Always upload SPIFFS data before uploading the main sketch
- If you make changes to files in the `/data` folder, remember to re-upload the filesystem image

### First-Time Setup

1. Power on your ESP32
2. Connect to the "EggTimer-Setup" WiFi network that appears
3. Follow the captive portal instructions to configure your WiFi credentials
4. The device will connect to your WiFi network and display its IP address on the Serial Monitor
5. You need to reboot the deivice to start the web service
6. Access the web interface by entering the IP address in your web browser
7. You can also access the device via mDNS at http://eggtimer.local/ if your system supports mDNS

## 🖥️ Web Interface

The web interface provides:

- Current temperature and humidity readings
- Threshold settings for temp and humidity
- Incubation timer display
- Statistical summaries for the last 24 hours and all time
- Interactive chart with temperature and humidity data
- Options to change the time range view (24 hours, 7 days, all data)
- "Start New Batch" button to reset all data
- Custom start time adjustment for existing incubations
- Data download option (JSON format)
- OTA firmware update interface

## 🔧 Configuration Options

You can customize the following in the code:

- `DHTPIN`: GPIO pin connected to the DHT sensor (default: 21)
- `DHTTYPE`: DHT sensor type (default: DHT22)
- `MAX_DATA_POINTS`: Maximum number of data points to store (default: 504, for 21 days at 1 hour intervals)
- `MAX_REASONABLE_TIMESTAMP`: Maximum acceptable timestamp for validation
- Data logging interval (default: 1 hour)

## 🧩 Advanced Features

### Data Storage

The system stores temperature and humidity data in the ESP32's SPIFFS file system. Data points are logged every hour, with a capacity for 21 days of historical data. The data is preserved across power cycles.

### OTA Updates

You can update the firmware without a USB connection:

1. Access the web interface
2. Click the "OTA Update" button
3. Select your new firmware .bin file
4. Click "Update" and wait for the process to complete

### Custom Start Time

For eggs already in incubation:

1. Enter the number of days and hours the eggs have been incubating
2. Click "Update Start Time"
3. Confirm the action (this will clear historical data)

## 🔍 Troubleshooting

- **WiFi Connection Issues**: Press the reset button on the ESP32 to restart the configuration process
- **Sensor Reading Errors**: Check the wiring and power supply to the DHT22 sensor
- **Time Synchronization Issues**: Ensure the ESP32 has internet access for NTP time synchronization
- **Data Not Logging**: Check the serial monitor for error messages
- **PlatformIO Build Errors**: Make sure all library dependencies are correctly specified in the platformio.ini file
- **ESP32 Not Found**: If using PlatformIO, try different USB ports or check if you need to install additional drivers for your specific ESP32 board

### Debugging Tips for PlatformIO

1. Enable verbose output for uploads by adding `upload_flags = -v` to platformio.ini
2. Check the "Terminal" tab in VS Code for detailed error messages
3. Use `Serial.printf()` statements for debugging complex data structures
4. If you're experiencing build issues with AsyncWebServer, try using the specific GitHub repository as shown in the platformio.ini example

## 📁 Project Structure

```
egg-incubuddy/
│
├── src/                      # Source code directory (for PlatformIO)
│   └── main.cpp              # Main program file
│
├── include/                  # Header files (for PlatformIO)
│
├── data/                     # SPIFFS files
│   ├── index.html            # Main web interface
│   ├── upload.html           # File upload interface
│   └── favicon.ico           # Browser tab icon
│
├── platformio.ini            # PlatformIO configuration
├── EggIncuBuddy.ino          # Arduino IDE main file
├── README.md                 # Project documentation
└── LICENSE                   # License file
```

## 🧰 Possible ToDo's

- [ ] Add support for multiple sensors
- [ ] Implement email/SMS alerts for temperature/humidity anomalies
- [ ] Add automatic humidity control via relay
- [ ] Create a mobile app interface
- [ ] Implement data backup to cloud services

## 📝 License

This project is licensed under the MIT License - see the LICENSE file for details.

## 🙏 Acknowledgments

- DHT sensor library by Adafruit
- WiFiManager by tzapu
- ESPAsyncWebServer by me-no-dev
- Chart.js for the beautiful interactive charts
- Bootstrap for the responsive UI framework
- Papa Lanc for the original project concept

## 🤝 Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add some amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## 📧 Contact

Project Creator - [Grey Lancaster](mailto:grey@smallbizserver.com)

Project Link: [https://github.com/Grey-Lancaster/incubuddy](https://github.com/Grey-Lancaster/IncuBuddy)
