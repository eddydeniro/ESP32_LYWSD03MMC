# ESP32_LYWSD03MMC
Edi Supriyadi

This project is for ESP32 and Thermometer LYWSD03MMC  
on Custom Firmware by Aaron Christopel (https://github.com/atc1441/ATC_MiThermometer)

## Features:

* Setting via classic bluetooth communication:

  ** WiFi credential (saved in EEPROM) -> enter SSID and password 
  
  ** server (saved in EEPROM) -> enter server name
  
  ** scan interval -> enter number (in seconds)
  
  ** network scan -> find network SSID around
  
  ** BLE device scan -> find mac address around
  
* Multithreading:

  ** Core 0 running for:
  
    *** WiFi -> sending data to server
    
    *** Classic bluetooth -> SETTING 
    
  ** Core 1 running for:
  
    *** BLE -> capturing service data from target devices


## Core process flow:

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
