# iot-SensorDriver
## Description:
Basic iot sensor setup, using io.adafruit.com as the cloud service for MQTT messages to be sent to.

Long term goals for this project: Develop a sensor firmware for ESP8266 or other wifi connected microcontroller and build out a dashboad and database integration through Heroku or similar.

## Required Hardware (sensor):
- ESP8266 based microcontroller
- Bosch BME-280 environmental sensor
- (Optional) SSD1306 or similar i2c LCD/OLED display for local display (without needing to go to online dashboard)

## Required Libraries:
* ESP8266 Arduino - Board Manager for Arduino IDE to connect to and upload code to the ESP8266
* ESP8266wifi - Part of the above package - secure client using TLS 1.1 available.
* Adafruit Sensor, Adafruit BME-280 - for interfacing with the BME-280 chip
* SSD1306 from https://github.com/squix78/esp8266-oled-ssd1306 for the 128x64 i2c OLED display
* Adafruit MQTT
