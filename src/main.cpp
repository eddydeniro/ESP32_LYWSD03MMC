/*
ESP32 - LYWSD03MMC
Edi Supriyadi
Feb 2021
v0.00
This project is for ESP32 and Thermometer LYWSD03MMC  
on Custom Firmware by Aaron Christopel (https://github.com/atc1441/ATC_MiThermometer)

Features:
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

Core process flow:
SETUP
* Connecting to network (reading EEPROM first for WiFi credential, if available, then using hardcoded one)
* Asking server for mac adresses of BLE devices to capture (TARGET DEVICE)

LOOP
* Scanning BLE devices for service data. First priority: TARGET DEVICE, otherwise just capturing devices with service data in advertising packet.
* Send service data to server

Parameters:
* Wifi credential (SSID and password, max 20 characters): changed via SETTING
* Server address (max 50 characters): changed via SETTING
* BLE scan failure limit : default to 3 times (hardcoded) -> restarting after the limit exceeded
* BLE scan interval: default to 60 s, changed via SETTING
* Target mac adresses (max 10 devices): supplied via server
* PIN 4 - 6 character

*/

//General
#include <Arduino.h>
#include <EEPROM.h>

//Bluetooth
#include <BLEDevice.h>
#include <BLE2902.h>
#define SERVICE_UUID        "a4a429f4-464a-443e-a3d0-43f3d5c0b7c6"
#define CHARACTERISTIC_UUID "134f4528-5233-429d-ab5d-49fdf506f461" 
char svcData[500];
char devices[10][20];
int failed_ble = 0;
int scode = 0;
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;  
bool deviceConnected = false;
bool oldDeviceConnected = false;
bool heap_report = false;
char pin[7] = "107890";
unsigned long last_access = 0;

//HTTP
#include <HTTPClient.h>
char ssid[21] = "ssid";
char password[21] = "password";
char serverName[51] = "http://example.com";
bool postNow = false;
bool gateway_init = false;

//LED
#define ONBOARD_LED 2

//Reference
int ginterval = 60; 
unsigned long gcur = ginterval * 1000;
unsigned long gprev = 0;

//Multitasking
TaskHandle_t Task0;
TaskHandle_t Task1;

char* get_chip_id(char* chip_id){
  uint64_t chipid = ESP.getEfuseMac(); // The chip ID is essentially its MAC address(length: 6 bytes)
  uint16_t chip = (uint16_t)(chipid >> 32);
  snprintf(chip_id, 23, "%04X%08X", chip, (uint32_t)chipid);
  return chip_id;  
}

bool confirm_device(char* mac_address){
  for (size_t i = 0; i < 10; i++)
  {
    if(devices[i][0] == 0){
      break;
    } 
    if(strcmp(devices[i], mac_address) == 0){
      return true;
    }
  }
  return false;
}
int listen_ble(char* message) {
  // const char returns[11][50] = {"COMMAND?", "OK! NEXT?", "AGAIN!", "SSID?", "PWD?", "INTV?", "SVR?", "PIN?", "LOGOUT!", "???", "WAIT!"};
  /*
  MENU
  1. WiFi credential (saved in EEPROM)
  2. Change scan interval (default 10 s, >=10 s)
  3. Change server name (saved in EEPROM)
  4. Report scanned devices to server
  5. Report heap size as BLE notification
  6. Change PIN
  */
  int respons = 9;
  if(scode == 0){
    //if pin match, ready to take command
    if(strcmp(message,pin)==0){
      last_access = millis();
      scode = 9;
      respons = 0;
    }
  } else if(scode == 49){
    last_access = millis();
    respons = 10;
  } else {
    last_access = millis();
    //Logged in
    if(scode == 9){
      //type "ok" or "0" for logout
      if(strcmp(message,"ok") == 0 || strcmp(message,"0") == 0){
        scode = 0;
        respons = 8;
      } else {
        switch (atoi(message))
        {
        case 1: //change WiFi credential
          scode = 11;
          respons = 3;
          break;
        case 2: //change scan interval (in s)
          scode = 21;
          respons = 5;
          break;
        case 3: //change server
          scode = 31;
          respons = 6;
          break;
        case 4: //report scanned device to server
          scode = 49;
          respons = 10;
          break;
        case 5: //report heap size as notification
          scode = 9;
          heap_report = !heap_report;
          respons = 1;
          break;
        case 6: //change pin
          scode = 61;
          respons = 7;
          break;          
        default:
          //type anything to go back to ready mode
          scode = 9;
          respons = 0;
          break;                                                      
        }
      }
    } else {
      //type "x" to cancel command in process
      if (strcmp(message,"x") == 0) {
        scode = 9;
        respons = 0;
      } else {
        switch (scode)
        {
        case 11:
          //validate SSID (4-20 chars)
          if(strlen(message)<5 || strlen(message)>20){
            respons = 2;
          } else {
            //waiting for PWD
            strcpy(ssid, message);
            scode = 12;        
            respons = 4;
          }
          break;
        case 12:
          //validate PWD (4-20 chars)
          if(strlen(message)<5 || strlen(message)>20){
            respons = 2;
          } else {
            strcpy(password, message);
            //Writing to EEPROM  
            for (int i = 0; i < 21; i++)
            {
              if(i < strlen(ssid)){
                EEPROM.write(i, ssid[i]);
              } else {
                EEPROM.write(i, 0);
              }
            }
            for (int i = 0; i < 21; i++)
            {
              if(i < strlen(password)){
                EEPROM.write(22+i, password[i]);
              } else {
                EEPROM.write(22+i, 0);
              }
            }
            scode = 9;        
            respons = 1;     
          }
          break;          
        case 21:
          //validate interval (>10 s)
          if(atoi(message) <= 10){
            respons = 2;
          }else{
            ginterval = atoi(message);
            scode = 9;
            respons = 1;           
          }        
          break;
        case 31:
          //validate server name (10-50 chars)
          if(strlen(message) < 10 || strlen(message) > 50){
            respons = 2;
          } else {
            char subbuff[5];
            strncpy(subbuff, message, 4);
            if (strcmp(subbuff,"http") == -1) {
              subbuff[4] = '\0';
              respons = 2;
            } else {
              //change server name
              strcpy(serverName, message);
              //write server name to EEPROM
              for (int i = 0; i < 51; i++)
              {
                if(i < strlen(message)){
                  EEPROM.write(43+i, message[i]);
                } else {
                  EEPROM.write(43+i, 0);
                }                
              }
              scode = 9;
              respons = 1;
            }
            subbuff[4] = '\0';
          }
          
          break;  
        case 61:
          //validate pin ( 4 - 6 chars)
          if(strlen(message)>6 || strlen(message)<5){
            respons = 2;
          } else {
            //change pin      
            strcpy(pin, message);
            for (int i = 0; i < 7; i++)
            {
              if(i < strlen(pin)){
                EEPROM.write(94+i, pin[i]);
              } else {
                EEPROM.write(94+i, 0);
              }
            }
            scode = 9;        
            respons = 1;
          }
          break;
        default:
          respons = 10;
          break;
        }
      }
    }
  }
  return respons;
}

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};

class MyBLECallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic){
    std::string value = pCharacteristic->getValue();
    char val[50];
    if (value.length() > 0) {
      for (int i = 0; i < value.length(); i++){
        val[i]=value[i];
      }
      val[value.length()] = '\0';
      int responds = listen_ble(val);
      const char returns[11][50] = {"COMMAND?", "OK! NEXT?", "AGAIN!", "SSID?", "PWD?", "INTV?", "SVR?", "PIN?", "LOGOUT!", "???", "WAIT!"};
      pCharacteristic->setValue(returns[responds]);
      pCharacteristic->notify();
    }
    val[0] = '\0';
  }
};
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice)
    {
      //Name only displayed at first scan
      //Serial.println(advertisedDevice.getName().c_str());
      String address = advertisedDevice.getAddress().toString().c_str();
      if(gateway_init == false || scode == 49){
        char tmp[25];
        ("*"+address).toCharArray(tmp, address.length() + 3);
        strcat(svcData, tmp);
        tmp[0] ='\0';
      } else {
        if(advertisedDevice.haveServiceData()){            
          std::string strServiceData = advertisedDevice.getServiceData();
          uint8_t cServiceData[100];            
          strServiceData.copy((char *)cServiceData, strServiceData.length(), 0);
          String Devdata;
          for (int i=0;i<strServiceData.length();i++) {
            String temp = String(cServiceData[i], HEX);
            if(temp.length()<2){
              temp = "0"+temp;
            }
            Devdata += temp;
          }
          char addr[20];
          strcpy(addr, address.c_str());
          if(confirm_device(addr)){
            char tmp[50];
            String serviceDt = "*" + Devdata;
            if(serviceDt.length()<50){
              //I think this is the culprit causing the bad memory heap if the allocated tmp size is less than the ServiceDt length.
              serviceDt.toCharArray(tmp, serviceDt.length() + 1);
              strcat(svcData, tmp);
            }
            tmp[0]='\0';
          }
          Devdata = "";
        }

        // char charValue[5] = {0,};
        // unsigned long value;
        // sprintf(charValue, "%02X%02X", cServiceData[6], cServiceData[7]);
        // value = strtol(charValue, 0, 16);            
        // float current_temperature = (float)value/10;
        // sprintf(charValue, "%02X", cServiceData[8]);
        // value = strtol(charValue, 0, 16);                    
        // float current_humidity = (float)value; 
        
        // Serial.print("\nTemperature: ");
        // Serial.print(current_temperature);
        // Serial.print("\nHumidity: ");
        // Serial.print(current_humidity);
        // Serial.println("");
      }          
    }
};

void send_data(){
    if(WiFi.status() == WL_CONNECTED){
      String action = "save";
      if(gateway_init == false || scode == 49){
        action="setup";
      }
      digitalWrite(ONBOARD_LED, HIGH);
      char chip_id[24];
      HTTPClient http;
      http.begin(serverName);
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      String httpRequestData = "key=tPmAT5Ab3j7F9&act="+action+"&g="+String(get_chip_id(chip_id))+"&dt="+String(svcData);

      Serial.println(httpRequestData);
      
      int httpResponseCode = http.POST(httpRequestData);    
      
      String respond = http.getString();

      if(gateway_init == false && httpResponseCode==200){
        gateway_init = true;  
        //Immediately capture BLE devices for service data
        gcur = ginterval * 1000;
        gprev = 0;
        if(respond != "NA"){
          char resp[200]; 
          respond.toCharArray(resp, respond.length() + 1);
          char * pch;
          pch = strtok (resp,";");
          int i=0;
          while (pch != NULL)
          {
            strcpy(devices[i], pch);
            pch = strtok (NULL, ";");
            i++;
          }
          resp[0]='\0';
        }        
      }
      if(action == "save" && httpResponseCode == 200){
        Serial.println(respond);
      }

      // Free resources
      http.end();
    }
    else {
      Serial.println(F("WiFi disconnected!"));
    }  
    digitalWrite(ONBOARD_LED, LOW);
}

void Task0code( void * pvParameters ){
  char esid[21];
  char epwd[21];
  //Reading ssid from EEPROM
  for (int i = 0; i < 21; i++)
  {
    uint8_t r = EEPROM.read(i);
    esid[i] = char(r);
    if(r == 0){
      break;
    }
  }
  if(strlen(esid)>=4){
    for (int i = 0; i < 21; i++)
    {
      epwd[i] = char(EEPROM.read(22+i));
    }
    if(strlen(epwd)>=4){    
      strcpy(ssid, esid);
      strcpy(password, epwd);
    }
  }
  WiFi.begin(ssid, password);
  esid[0]='\0';
  epwd[0]='\0';
  
  //Reading server name from EEPROM
  char esvr[51];
  for (int i = 0; i < 51; i++)
  {
    uint8_t r = EEPROM.read(43+i);
    esvr[i] = char(r);
    if(r == 0){
      break;
    }
  }
  if(strlen(esvr) >= 10){
    strcpy(serverName, esvr);
  }
  esvr[0]='\0';

  //Reading PIN from EEPROM
  char epin[7];
  for (int i = 0; i < 7; i++)
  {
    uint8_t r = EEPROM.read(94+i);
    epin[i] = char(r);
    if(r == 0){
      break;
    }
  }
  if(strlen(epin) >= 4){
    strcpy(pin, epin);
  }
  epin[0]='\0';

  for(;;){
    if(WiFi.status() == WL_CONNECTED){
      if(postNow){     
        send_data(); 
        postNow = false;
      }
    }
    //This delay is important to keep multi core task stabilized
    delay(1);
  } 
}

void Task1code( void * pvParameters ){
  char chip_id[24];
  get_chip_id(chip_id);
  Serial.begin(115200); 
  char x[25] = "LAT_3DO_";
  strcat(x, chip_id);
  BLEDevice::init(x);

  BLEScan *pBLEScan = BLEDevice::getScan(); //create new scan 
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);  // less or equal setInterval value      

  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                        CHARACTERISTIC_UUID,
                                        BLECharacteristic::PROPERTY_READ |
                                        BLECharacteristic::PROPERTY_WRITE |
                                        BLECharacteristic::PROPERTY_NOTIFY //|
                                        // BLECharacteristic::PROPERTY_INDICATE                
                                       );
  // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.client_characteristic_configuration.xml
  // Create a BLE Descriptor
  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setCallbacks(new MyBLECallback());
  pService->start();
  // BLEAdvertising *pAdvertising = pServer->getAdvertising();  // this still is working for backward compatibility
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println(F("3DO - ESP32"));
  Serial.println(chip_id);
  Serial.println(F("Starting up..."));

  pinMode (ONBOARD_LED,OUTPUT);

  for(;;){
    if(gcur - gprev >= (ginterval*1000) || (scode==49) || failed_ble > 0) {
      if(gprev == 0){
        gprev = millis();
      } else {
        gprev = gcur;
      }    
      svcData[0] = '\0';

      char buff[6];
      sprintf(buff, "%d", esp_get_free_heap_size());
      Serial.println();
      Serial.print("Free HeapSize: ");
      Serial.println(esp_get_free_heap_size());     
      // notify changed value
      if (deviceConnected) {
          if(heap_report && scode == 0){
            pCharacteristic->setValue(buff);
            pCharacteristic->notify();
          }
      }
      buff[0]='\0';
      Serial.println(F("Scanning..."));
      BLEScanResults foundDevices = pBLEScan->start(10);
      Serial.println(F("Done!"));   
      if(foundDevices.getCount()>0){
        postNow = true;
        failed_ble = 0;
      } else {
        Serial.println(F("No device detected!"));   
        failed_ble++;  
        if(failed_ble >= 3){
          ESP.restart();
        }   
      }
    }
    if(postNow == false && scode == 49){
      pCharacteristic->setValue("OK! NEXT?");
      pCharacteristic->notify();                    
      scode = 9;
    }
    // disconnecting
    if (!deviceConnected && oldDeviceConnected) {
        delay(500); // give the bluetooth stack the chance to get things ready
        pServer->startAdvertising(); // restart advertising
        Serial.println("start advertising");
        oldDeviceConnected = deviceConnected;
    }
        // connecting
    if (deviceConnected && !oldDeviceConnected) {
        // do stuff here on connecting
        oldDeviceConnected = deviceConnected;
    }    
    if(scode != 0){
      Serial.println(millis() - last_access);                  
      if(millis() - last_access >=120000){
        scode = 0;
      }
    }
    gcur = millis();
  }
}

void setup() {
  //create a task that will be executed in the Task1code() function, with priority 1, on core 0
  xTaskCreatePinnedToCore(Task0code, "Task0", 10000, NULL, 1, &Task0, 0);                  
  delay(1000); 
  //create a task that will be executed in the Task2code() function, with priority 1, on core 1
  xTaskCreatePinnedToCore(Task1code, "Task1", 10000, NULL, 1, &Task1,1);
  delay(5); 
}

void loop() {
  //This delay is important to stabilize multithreading
  //Normal loop is on core 1
  delay(1);  
}