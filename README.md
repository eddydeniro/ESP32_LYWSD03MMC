# ESP32_LYWSD03MMC
Edi Supriyadi

This project is for ESP32 and Thermometer LYWSD03MMC  
on custom firmware by [Aaron Christopel](https://github.com/atc1441/ATC_MiThermometer)

## Features:  
### Setting via bluetooth LE:  
 MENU:  
 1. WiFi credential (saved in EEPROM)
 2. Change scan interval (default 10 s, >=10 s)
 3. Change server name (saved in EEPROM)
 4. Report scanned devices to server
 5. Report heap size as BLE notification
 6. Change PIN

### Multithreading:
* Core 0 running for:
  * WiFi -> sending data to server
* Core 1 running for:
  * BLE -> capturing service data from target devices and setting parameters

## Core Process Flow:
SETUP
* Connecting to network (reading EEPROM first for WiFi credential, if available, then using hardcoded one)
* Asking server for mac adresses of BLE devices to capture (TARGET DEVICE)

LOOP
* Scanning BLE devices for service data. First priority: TARGET DEVICE, otherwise just capturing devices with service data in advertising packet.
* Send service data to server

## Parameters:  
* Wifi credential (SSID and password, max 20 characters): changed via SETTING
* Server address (max 50 characters): changed via SETTING
* BLE scan failure limit : default to 3 times (hardcoded) -> restarting after the limit exceeded
* BLE scan interval: default to 60 s, changed via SETTING
* Target mac adresses (max 10 devices): supplied via server
* PIN to enter SETTING

## Development Environment
This project is written in VS Code with platformIO.

**Arduino IDE**
* Delete line _#include <Arduino.h>_
* Don't forget to setup **partition**. Otherwise you'll get oversize warning
* Depending the ESP32 BLE library you are using, if you get warning about _advertisedDevice.getServiceData()_, just change to _advertisedDevice.getServiceData(0)_ 
