/*
ESP32 - LYWSD03MMC
Edi Supriyadi
Feb 2021
v0.00
This project is for ESP32 and Xiaomi Thermometer LYWSD03MMC  
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

*/

//General
#include <EEPROM.h>

//Bluetooth
#include <BLEDevice.h>
#include <BluetoothSerial.h>
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

  //*BLE
  char svcData[350];
  char devices[10][20];
  int dtIndex = 0;
  bool viewBLE = false;
  int failed_ble = 0;

  //*Classic BL
  BluetoothSerial SerialBT;
  int BT_com = 0;

//HTTP
#include <HTTPClient.h>
char ssid[21] = "deearado_plus";
char password[21] = "3107@k1R";
char serverName[51] = "http://192.168.100.50/ruko28/rest.php";
char chip_id[23];
bool postNow = false;

//LED
#define ONBOARD_LED 2

//Reference
int ginterval = 60; 
unsigned long gcur = ginterval * 1000;
unsigned long gprev = 0;

//Multitasking
TaskHandle_t Task0;
TaskHandle_t Task1;

bool confirm_device(char* mac_address){
  int dcount = sizeof(devices);
  if(dcount > 0){
    for (size_t i = 0; i < dcount; i++)
    {
      if(strcmp(devices[i], mac_address) == 0){
        return true;
      }
    }
    return false;
  } else {
    return true;
  }
}
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice)
    {
      String address = advertisedDevice.getAddress().toString().c_str();
      if(viewBLE){
        SerialBT.println(address);
      } else {
        if(advertisedDevice.haveServiceData()){            
          std::string strServiceData = advertisedDevice.getServiceData(0);
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
            char tmp[] = "";
            String serviceDt = "&v" + String(dtIndex) + "=" + Devdata;
            serviceDt.toCharArray(tmp, serviceDt.length() + 1);
            strcpy(svcData, tmp);
            dtIndex++;
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


void connect_ap(bool new_credential){
  char esid[21];
  char epwd[21];
  //Reading EEPROM
  for (int i = 0; i < 21; i++)
  {
    uint8_t r = EEPROM.read(i);
    esid[i] = char(r);
    if(r == 0){
      break;
    }
  }
  if(strlen(esid)>0){
    for (int i = 0; i < 21; i++)
    {
      epwd[i] = char(EEPROM.read(22+i));
    }
    WiFi.begin(esid, epwd);    
    strcpy(ssid, esid);
    strcpy(password, epwd);    
  } else {
    WiFi.begin(ssid, password);
  }
  
  Serial.println(F("Connecting to AP..."));
  int i = 0;
  while(WiFi.status() != WL_CONNECTED) {
    if(i >= 50){
      Serial.println(F("WIFI failed!"));     
      break;
    } else {
      delay(100);
    }
    i++;
  }  
  if(WiFi.status() == WL_CONNECTED){
    Serial.println(F("WIFI connected!"));
    if(new_credential){
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
    }
  }    
}

//action: setup or save
void send_data(String action){
    if(WiFi.status() == WL_CONNECTED){
      digitalWrite(ONBOARD_LED, HIGH);

      HTTPClient http;
      http.begin(serverName);
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      String httpRequestData = "key=tPmAT5Ab3j7F9&act="+action+"&g="+String(chip_id);   
      if(action=="save"){
        httpRequestData += String(svcData);
      }   
      int httpResponseCode = http.POST(httpRequestData);    
      String respond = http.getString();
              
      if(action == "setup" && httpResponseCode==200 && respond != "NA"){
        char resp[] = ""; 
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
      }
      if(action == "save" && httpResponseCode == 200){
        Serial.println(F("Data saved!"));
      }
      // Free resources
      http.end();
    }
    else {
      Serial.println(F("WiFi disconnected!"));
    }  
    digitalWrite(ONBOARD_LED, LOW);
}

char message[51]; // Allocate some space for the string, max 50 plus terminating character

void Task0code( void * pvParameters ){
  for(;;){
    if(postNow){
      if(WiFi.status() == WL_CONNECTED){      
        send_data("save");
      }
      postNow = false;
    }
    if (SerialBT.available()) {
      char incomingChar = SerialBT.read();
      if (incomingChar != '\n'){
        message[strlen(message)] = incomingChar;
      }
      else{
        char BT_key[] = "107890";
        if(strcmp(message, "x") == 0){
          BT_com = 0;
        } else {
          switch (BT_com)
          {
            case 1:
              switch (atoi(message))
              {
              case 1:
                SerialBT.println(F("SSID?"));
                BT_com = 11;
                break;
              case 2:
                SerialBT.println(F("INTV?"));
                BT_com = 21;
                break;
              case 3:
                SerialBT.println(F("SVR?"));
                BT_com = 31;
                break;
              case 4:
                //WIFI SCANNING;
                {
                  SerialBT.println(F("Scanning WIFI..."));
                  int nw = WiFi.scanNetworks();
                  if (nw == 0){
                    SerialBT.println(F("No networks found!"));
                  } else {
                    for (int i = 0; i < nw; ++i)
                    {
                      SerialBT.println(WiFi.SSID(i));
                      delay(10);
                    }
                  }
                }
                BT_com = 0;
                break;
              case 5:
                //BLE SCANNING;
                SerialBT.println(F("Scanning BLE..."));
                viewBLE = true;
                BT_com = 0;
                break;  
              default:
                BT_com = 0;
                break;                                                      
              }            
              break;
            case 11:
              if(strlen(message)>20){
                SerialBT.println(F("NO!"));
                BT_com = 0;
                break;
              }              
              strcpy(ssid, message);
              SerialBT.println(F("PWD?"));
              BT_com = 12;
              break;  
            case 12:
              strcpy(password, message);
              SerialBT.println(F("OK!"));
              connect_ap(true);
              BT_com = 0;
              break;
            case 21:
              if(atoi(message) <= 10){
                SerialBT.println(F("NO!"));
              }else{
                ginterval = atoi(message);
                SerialBT.println(F("OK!"));                
              }
              BT_com = 0;
              break;          
            case 31:
              strcpy(serverName, message);
              for (int i = 0; i < 51; i++)
              {
                if(i < strlen(message)){
                  EEPROM.write(43+i, message[i]);
                } else {
                  EEPROM.write(43+i, 0);
                }                
              }
              SerialBT.println(F("OK!"));
              BT_com = 0;
              break;            
            case 0:
              if(strcmp(message,BT_key)==0){
                SerialBT.println(F("1 WIFI | 2 INTV | 3 SVR | 4 WSCN | 5 BSCN"));
                BT_com = 1;
              }
              break;
            default:
              BT_com = 0;
              break;
          }
        }
        message[0] = '\0';
      }    
    }
    //This delay is important to keep multi core task stabilized
    delay(1);
  } 
}

void Task1code( void * pvParameters ){
  uint64_t chipid = ESP.getEfuseMac(); // The chip ID is essentially its MAC address(length: 6 bytes)
  uint16_t chip = (uint16_t)(chipid >> 32);
  snprintf(chip_id, 23, "%04X%08X", chip, (uint32_t)chipid);

  Serial.begin(115200); 
  SerialBT.begin(chip_id);
  BLEDevice::init("");

  Serial.println(F("3DO - ESP32"));
  Serial.println(chip_id);
  Serial.println(F("Starting up..."));

  pinMode (ONBOARD_LED,OUTPUT);

  connect_ap(false);
  if(WiFi.status() == WL_CONNECTED){
    send_data("setup");
  }
  char esvr[51];
  for (int i = 0; i < 51; i++)
  {
    uint8_t r = EEPROM.read(43+i);
    esvr[i] = char(r);
    if(r == 0){
      break;
    }
  }
  if(strlen(esvr) > 7){
    strcpy(serverName, esvr);
  }

  for(;;){
    if(gcur - gprev >= (ginterval*1000) || viewBLE){
      if(gprev == 0){
        gprev = millis();
      } else {
        gprev = gcur;
      }    
      svcData[0] = '\0';
      dtIndex = 0;
      Serial.println(F("Scanning..."));
      BLEScan *pBLEScan = BLEDevice::getScan(); //create new scan 
      pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
      pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
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
      pBLEScan->clearResults();//to release memory
      if(viewBLE){
        viewBLE = false;        
      }
    }
    gcur = millis();
  }
}

void setup() {
  //create a task that will be executed in the Task1code() function, with priority 1, on core 0
  xTaskCreatePinnedToCore(Task0code, "Task0", 10000, NULL, 1, &Task0, 0);                  
  delay(5); 
  //create a task that will be executed in the Task2code() function, with priority 1, on core 1
  xTaskCreatePinnedToCore(Task1code, "Task1", 10000, NULL, 1, &Task1,1);
  delay(5); 
}

void loop() {
  //This delay is important to stabilize multithreading
  //Normal loop is on core 1
  delay(1);  
}
