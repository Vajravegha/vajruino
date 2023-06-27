// This example code demonstrates how to read Modbus RTU data from Modbus RTU slaves and save it to an SD card along with a timestamp.
// Additionally, it shows how to send the data to an MQTT broker (HiveMQ)in json format via Cellular Network
// This code requires a Modbus RTU Slave device to be connected to the RS485 port. Here slave ID is set to 1, Function Code is set to 4 (Register 30000 onwards) and we read data from two consecutive registers
// This code enables to publish test messages to the well known public broker by hivemq.com at http://www.hivemq.com/demos/websocket-client/
// Go to this webpage and click on Connect. Click on Add New Topic Subscription and type topic name as "VAJRUINO/TEST"
// If the module is connected to Internet, test messages will be published by the module and these messages will be displayed on the screen at regular intervals
//refer AT command datasheet
// Code is OTA enabled using Async Elegant OTA library
// Onboard USB to TTL is NOT present
// To flash/program ESP32 manually, use a TTL to USB Convertor, connect Tx, Rx and - of the Serial Monitor Port on the board to Rxd, Txd and Gnd respectively of the USB-TTL Module
// Short connectors J1(GPIO0) and J2(RESET) on the board using jumpers provided.
// After clicking on upload icon in Arduino IDE, when "Connecting....._____.....____" message appears remove the J2(reset) jumper and code begins to upload.
// After Code is Done Uploading, remove J1(GPIO0) Jumper. Once again, short and remove J2 jumper momentarily to restart ESP32
// If code fails to upload, remove SD card and try again
//FOR VVM401 PRODUCT DETAILS VISIT www.vv-mobility.com

#define TINY_GSM_MODEM_SIM7600  // SIM7600 AT instruction is compatible with A7672
#define SerialAT Serial1
#define TINY_GSM_USE_GPRS true
#include <WiFi.h>
#include <WiFiClient.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <TinyGsmClient.h>
#include <Wire.h>
#include <EEPROM.h>
#include <SPI.h>
#include <SD.h>
#include <RTClib.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ModbusRtu.h>  //https://github.com/smarmengol/Modbus-Master-Slave-for-Arduino

// MQTT Broker details
const char* mqttServer = "broker.hivemq.com";
const int mqttPort = 1883;
const char* mqttTopic = "VAJRUINO/TEST";
const char* mqttClientId = "VAJRUINO_Client";
char dataEntry[2048];

// Pin Definitions

#define RXD1 27    //4G MODULE RXD INTERNALLY CONNECTED, Hardware Serial 1
#define TXD1 26    //4G MODULE TXD INTERNALLY CONNECTED, Hardware Serial 1
#define powerPin 4 ////4G MODULE ESP32 PIN D4 CONNECTED TO POWER PIN OF A7670C CHIPSET, INTERNALLY CONNECTED
#define RXD2 32 // RS485 or RS232, Hardware Serial2 
#define TXD2 33 // RS485 or RS232, Hardware Serial2

const int statusLED = 12; // Onboard LED
int rx = -1;
String rxString;
AsyncWebServer server(80);
const char* ssid = "iotgateway"; // Replace this with your SSID Password so that device can connect to WiFi. This is required for updating ESP32 code via OTA
const char* password = "iot@1234";  // minimum 8 digit
const char apn[]      = ""; //APN automatically detects for 4G SIM, NO NEED TO ENTER, KEEP IT BLANK

// data array for modbus network sharing
uint16_t au16data[16];

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, Serial);
TinyGsm        modem(debugger);
#else
TinyGsm        modem(SerialAT);
#endif

// Digital pin numbers
//Input Pins
#define DI1 35
#define DI2 34
#define DI3 39
#define DI4 36
//Output Pins, can also be used as alternative Serial Tx and Rx
#define DO1 17
#define DO2 16

#define SD_CS_PIN 15  // SD Card uses HSPI pins and Ethernet uses default VSPI
#define SD_CLK_PIN 14
#define SD_MOSI_PIN 13
#define SD_MISO_PIN 2


// Initialize Objects
TinyGsmClient LTEClient(modem);
PubSubClient  mqttLTEClient(LTEClient);
RTC_DS3231 rtc; //RTC is connected to default I2C pins 21(SDA) and 22(SCL) of ESP32
SPIClass spi2(HSPI);  //SD Card
uint8_t SDCardError = 0;
Modbus master(0, Serial2); // //Modbus master ID, connected to RS485 via TXD2 (33) and RXD2 (32)
 modbus_t telegram;

StaticJsonDocument<2048> JSONencoder;
char jsonBuffer[2048];

void setup() {

  // Enable the 4G chipset
  pinMode(powerPin, OUTPUT);
  digitalWrite(powerPin, LOW);
  pinMode(statusLED, OUTPUT);
  digitalWrite(statusLED, LOW);
  pinMode(DO1, OUTPUT); // DO1 and DO2 are Digital Pins 17 and 16 of ESP32 which can also be used as Alternate Serial depending on your application
  digitalWrite(DO1, LOW);
  pinMode(DO2, OUTPUT);
  digitalWrite(DO2, LOW);
  pinMode(DI1, INPUT);
  pinMode(DI2, INPUT);
  pinMode(DI3, INPUT);
  pinMode(DI4, INPUT);
  
  // Initialize Serial Communication
  Serial.begin(115200); //Default Serial Monitor
  SerialAT.begin(115200, SERIAL_8N1, RXD1, TXD1); //Serial 1 for 4G
  Serial2.begin(9600,  SERIAL_8N1, RXD2, TXD2); //Serial 2 for RS485 Modbus / RS232
  delay(10);
  
  master.start(); //Modbus master
  master.setTimeOut( 2000 ); // if there is no answer in 2000 ms, roll over
  
  // Fetch RTC Data
  if (!rtc.begin()) {
    Serial.println("RTC initialization failed!");
    //return; //CHECK RETURN FOR BOTH SD AND RTC
  }
  else  {
    Serial.println("RTC initialized.");
    DateTime now = rtc.now();
    String timestamp = now.timestamp(DateTime::TIMESTAMP_FULL);
    Serial.print("RTC Time: ");
    Serial.println(timestamp);
    //rtc.adjust(DateTime(F(__DATE__),F(__TIME__)));
  }

  mqttLTEClient.setServer(mqttServer, mqttPort);

  // Initialize SD Card
  spi2.begin(SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, spi2, 80000000)) {
    Serial.println("Card Mount Failed");
    SDCardError = 1;
  }
  else
  {
    Serial.println("SD card initialized.");
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
      Serial.println("No SD card attached");
      SDCardError = 1;
    }
    else
    {
      Serial.print("SD Card Type: ");
      if (cardType == CARD_MMC) {
        Serial.println("MMC");
      } else if (cardType == CARD_SD) {
        Serial.println("SDSC");
      } else if (cardType == CARD_SDHC) {
        Serial.println("SDHC");
      } else {
        Serial.println("UNKNOWN");
      }
    }
  }

  // check WiFi...
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid, password);
  checkWiFi();
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", "Hi, this is IoT Gateway Board");
  });
  AsyncElegantOTA.begin(&server);    // Start ElegantOTA
  server.begin();
  //check 4G module and Internet Connectivity
  check4Gmodule();


}

void loop() {
 
  DateTime now = rtc.now();
  uint32_t unixTime = now.unixtime();
  Serial.println("Fetching Modbus Data");
  int pollI = 0, pollR = 0, q = 0;
  telegram.u8id = 1; // Enter Modbus Slave ID here
  telegram.u8fct = 4; // function code (this one is registers read, so register 30000 onwards)
  telegram.u16RegAdd = 0; // start address in slave
  telegram.u16CoilsNo = 2; // number of elements (coils or registers) to read
  telegram.au16reg = au16data; // pointer to a memory array in the Arduino
  q = master.query( telegram ); // send query (only once)
  while(pollI < 100)
  {
    pollI++;
    pollR = master.poll();
    delay(10);
    if (pollR != 0)
      break;
  }
  Serial.print("Poll result is ");
  Serial.println(pollR);
  Serial.print("First register: ");
  Serial.println(au16data[0]);
  Serial.print("Second register: ");
  Serial.println(au16data[1]);
  
  // Read the status of Vajruino digital input pins,  optically isolated and active Low
  int statusDI1 = digitalRead(DI1);
  int statusDI2 = digitalRead(DI2);
  int statusDI3 = digitalRead(DI3);
  int statusDI4 = digitalRead(DI4);
  //Build data into json format for transmitting
  JSONencoder["TS"] = unixTime;
  JSONencoder["Reg1"] = au16data[0];
  JSONencoder["Reg2"] = au16data[1];
  JSONencoder["DI1"] = statusDI1;
  JSONencoder["DI2"] = statusDI2;
  JSONencoder["DI3"] = statusDI3;
  JSONencoder["DI4"] = statusDI4;
  serializeJson(JSONencoder, jsonBuffer);
  Serial.println(jsonBuffer);
  // Publish data to MQTT broker via Cellular Network
  if (mqttLTEClient.connect(mqttClientId)) {
    
    // Publish data to MQTT broker
    if (mqttLTEClient.publish(mqttTopic, jsonBuffer)) {
      Serial.println("Data published to MQTT broker");
      digitalWrite(statusLED, HIGH); // LED blink
      delay(250);
      digitalWrite(statusLED, LOW);
    } else {
      Serial.println("Failed to publish data to MQTT broker");
      digitalWrite(statusLED, HIGH); // Blink LED Twice if not published
      delay(250);
      digitalWrite(statusLED, LOW);
      delay(250);
      digitalWrite(statusLED, HIGH);
      delay(250);
      digitalWrite(statusLED, LOW);
    }

  }
  mqttLTEClient.loop();
  delay(5000);


  if (SDCardError == 0)
  {
    String logEntry = String(jsonBuffer);
    logEntry = String(logEntry + "\n");

    logEntry.toCharArray(dataEntry, (logEntry.length() + 1)); // +1 because .length doesnt consider trailng null character
    //Serial.println(logEntry);
    //Serial.println(dataEntry);
    appendFile(SD, "/data.csv", dataEntry);
  }
}

void checkWiFi()
{
  WiFi.disconnect();
  delay(100);
  WiFi.reconnect();
  delay(100);
  Serial.print("Connecting to WiFi");
  int counter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(500);
    counter++;
    if (counter >= 25)
    {
      Serial.println("No WiFi");
      WiFi.mode(WIFI_AP_STA);
      return;
    }
  }
  Serial.println("WiFi connected as");
  Serial.println(WiFi.localIP());
  delay(100);
}

void check4Gmodule()
{
  digitalWrite(powerPin, LOW);
  delay(100);
  digitalWrite(powerPin, HIGH);
  delay(1000);
  digitalWrite(powerPin, LOW);

  Serial.println("\nconfiguring 4G Module. Kindly wait");
  delay(10000);

  // Restart takes quite some time
  // To skip it, call init() instead of restart()
  DBG("Initializing modem...");
  if (!modem.init()) {
    DBG("Failed to restart modem");
    return;
  }
  // Restart takes quite some time
  // To skip it, call init() instead of restart()

  String name = modem.getModemName();
  DBG("Modem Name:", name);

  String modemInfo = modem.getModemInfo();
  DBG("Modem Info:", modemInfo);

  Serial.println("Waiting for network...");
  if (!modem.waitForNetwork()) {
    Serial.println(" fail");
    delay(10000);
    return;
  }
  Serial.println(" success");
  delay(15000);
  if (modem.isNetworkConnected()) {
    Serial.println("Connected to Network");
  }
  else
    Serial.println("No Network");


  // GPRS connection parameters are usually set after network registration
  Serial.print(F("Connecting to 4G"));
  Serial.print(apn);
  if (!modem.gprsConnect(apn)) {
    Serial.println(" failed");
    return;
  }
  Serial.println(" success");

  if (modem.isGprsConnected()) {
    Serial.println("LTE Internet connected");
  }
  else
  {
    Serial.println("No LTE Internet");
  }

}


///////////////SD Card Functions////////////////////////////
void listDir(fs::FS &fs, const char * dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.name(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

void createDir(fs::FS &fs, const char * path) {
  Serial.printf("Creating Dir: %s\n", path);
  if (fs.mkdir(path)) {
    Serial.println("Dir created");
  } else {
    Serial.println("mkdir failed");
  }
}

void removeDir(fs::FS &fs, const char * path) {
  Serial.printf("Removing Dir: %s\n", path);
  if (fs.rmdir(path)) {
    Serial.println("Dir removed");
  } else {
    Serial.println("rmdir failed");
  }
}

void readFile(fs::FS &fs, const char * path) {
  Serial.printf("Reading file: %s\n", path);

  File file = fs.open(path);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.print("Read from file: ");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
}

void writeFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Writing file: %s\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}

void appendFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Appending to file: %s\n", path);

  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if (file.print(message)) {
    Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
  file.close();
}

void renameFile(fs::FS &fs, const char * path1, const char * path2) {
  Serial.printf("Renaming file %s to %s\n", path1, path2);
  if (fs.rename(path1, path2)) {
    Serial.println("File renamed");
  } else {
    Serial.println("Rename failed");
  }
}

void deleteFile(fs::FS &fs, const char * path) {
  Serial.printf("Deleting file: %s\n", path);
  if (fs.remove(path)) {
    Serial.println("File deleted");
  } else {
    Serial.println("Delete failed");
  }
}

void testFileIO(fs::FS &fs, const char * path) {
  File file = fs.open(path);
  static uint8_t buf[512];
  size_t len = 0;
  uint32_t start = millis();
  uint32_t end = start;
  if (file) {
    len = file.size();
    size_t flen = len;
    start = millis();
    while (len) {
      size_t toRead = len;
      if (toRead > 512) {
        toRead = 512;
      }
      file.read(buf, toRead);
      len -= toRead;
    }
    end = millis() - start;
    Serial.printf("%u bytes read for %u ms\n", flen, end);
    file.close();
  } else {
    Serial.println("Failed to open file for reading");
  }


  file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }

  size_t i;
  start = millis();
  for (i = 0; i < 2048; i++) {
    file.write(buf, 512);
  }
  end = millis() - start;
  Serial.printf("%u bytes written for %u ms\n", 2048 * 512, end);
  file.close();
}

///////////////////end of SD Card Functions /////////////////////////////
