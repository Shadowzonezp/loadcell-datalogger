// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

//
// Shows how to serve a static file
//

#include <Arduino.h>
#include <AsyncTCP.h>
#include <WiFi.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
//#include <LittleFS.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <HX711_ADC.h>

//pins:
const int HX711_dout = 17; //mcu > HX711 dout pin
const int HX711_sck = 16; //mcu > HX711 sck pin

//HX711 constructor:
HX711_ADC LoadCell(HX711_dout, HX711_sck);

Preferences preferences;
/*
Uncomment and set up if you want to use custom pins for the SPI communication
*/
#define REASSIGN_PINS
int sck = 15;
int miso = 32;
int mosi = 33;
int cs = -1;

int endmillis = 0;
int startmillis = 0;

bool loggingActive = false;

unsigned long t = 0;

static AsyncWebServer server(80);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "2.europe.pool.ntp.org",3600*2); // 3600*2 is for timezone GMT+2

void initSDCard(){
#ifdef REASSIGN_PINS
  SPI.begin(sck, miso, mosi, cs);
  if (!SD.begin(cs)) {
#else
  if (!SD.begin()) {
#endif
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE) {
    Serial.println("ERROR: No SD card attached");
    return;
  }

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
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
}

void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
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
        listDir(fs, file.path(), levels - 1);
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

void tare(AsyncWebServerRequest *request){
  Serial.println("called tare");
  // display params
  LoadCell.update();
  LoadCell.tareNoDelay();
  boolean _resume = false;
  while (_resume == false) {
    LoadCell.update();
    if (LoadCell.getTareStatus() == true) {
      Serial.println("Tare complete");
      _resume = true;
    }
  }
  request->send(200, "text/html", "ok" );
}

void hometare(AsyncWebServerRequest *request){
  Serial.println("called hometare");
  unsigned long stabilizingtime = 2000; // preciscion right after power-up can be improved by adding a few seconds of stabilizing time
  boolean _tare = true; //set this to false if you don't want tare to be performed in the next step
  LoadCell.start(stabilizingtime, _tare);
  request->send(200, "text/html", "ok" );
}

void calibrate(AsyncWebServerRequest *request){
  float known_mass = 0;
  Serial.println("called calibrate");
  size_t count = request->params();
  for (size_t i = 0; i < count; i++) {
    const AsyncWebParameter* p = request->getParam(i);
    Serial.printf("PARAM[%u]: %s = %s\n", i, p->name().c_str(), p->value().c_str());
  }
  const AsyncWebParameter* param = request->getParam("mass");
  if (param != nullptr){
    //preferences.putUInt("calvalue", param->value().toInt());
    known_mass = param->value().toFloat();
    Serial.println(known_mass);
  }
  LoadCell.refreshDataSet(); //refresh the dataset to be sure that the known mass is measured correct
  float newCalibrationValue = LoadCell.getNewCalibration(known_mass); //get the new calibration value
  Serial.println(newCalibrationValue);
  preferences.putFloat("calvalue", newCalibrationValue);
  request->redirect( "/index.html" );
}

void start(AsyncWebServerRequest *request) {
  loggingActive = true;
  startmillis = millis();
  Serial.println("Logging started");
  request->send(200, "text/html", "ok");
}

// Buffer for storing load cell readings (15KB for ESP32)
#define BUFFER_SIZE 15000
struct LoadCellSample {
    unsigned long timestamp;
    float value;
};
constexpr size_t SAMPLE_SIZE = sizeof(LoadCellSample); // 8 bytes per sample (4+4)
constexpr size_t MAX_SAMPLES = BUFFER_SIZE / SAMPLE_SIZE;
LoadCellSample sampleBuffer[MAX_SAMPLES];
volatile size_t sampleCount = 0;

void printSampleBuffer() {
    Serial.println("=== Sample Buffer Contents ===");
    for (size_t i = 0; i < sampleCount; i++) {
        Serial.print("Sample ");
        Serial.print(i);
        Serial.print(": millis=");
        Serial.print(sampleBuffer[i].timestamp);
        Serial.print(", value=");
        Serial.println(sampleBuffer[i].value, 6);
    }
    Serial.println("=== End of Buffer ===");
}

void stop(AsyncWebServerRequest *request) {
  loggingActive = false;
  endmillis = millis();
  Serial.println("Logging stopped");
  int milliselapsed = endmillis - startmillis;
  Serial.println("Duration in millis:");
  Serial.println(milliselapsed);

  // Print buffer contents for debugging
  printSampleBuffer();

  // ==== Save buffer to SD card ====
  // Create "recordings" directory if it doesn't exist
  if (!SD.exists("/recordings")) {
    SD.mkdir("/recordings");
  }

  // Use NTP time at start of recording for filename
  time_t startEpoch = timeClient.getEpochTime() - ((millis() - startmillis) / 1000);
  struct tm *tmstruct = gmtime(&startEpoch);
  char timeString[32];
  strftime(timeString, sizeof(timeString), "%Y%m%d_%H%M%S", tmstruct);

  char filename[64];
  snprintf(filename, sizeof(filename), "/recordings/rec_%s.csv", timeString);

  File file = SD.open(filename, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing!");
  } else {
    // Write CSV header
    file.println("timestamp,value");
    // Write all samples
    for (size_t i = 0; i < sampleCount; i++) {
      file.print(sampleBuffer[i].timestamp);
      file.print(",");
      file.println(sampleBuffer[i].value, 6);
    }
    file.close();
    Serial.print("Saved buffer to SD: ");
    Serial.println(filename);
  }

  // Optionally clear the buffer after saving
  sampleCount = 0;

  request->send(200, "text/html", "ok");
}

void setup() {
  Serial.begin(115200);
// WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
  // it is a good practice to make sure your code sets wifi mode how you want it.
  // put your setup code here, to run once:
  Serial.begin(115200);
  initSDCard();
  listDir(SD, "/", 0);
    
  //WiFiManager, Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wm;

  // reset settings - wipe stored credentials for testing
  // these are stored by the esp library
  // wm.resetSettings();

  // Automatically connect using saved credentials,
  // if connection fails, it starts an access point with the specified name ( "AutoConnectAP"),
  // if empty will auto generate SSID, if password is blank it will be anonymous AP (wm.autoConnect())
  // then goes into a blocking loop awaiting configuration and will return success result

  bool res;
  // res = wm.autoConnect(); // auto generated AP name from chipid
  // res = wm.autoConnect("AutoConnectAP"); // anonymous ap
  res = wm.autoConnect("sigmaaccespoint","n0maswifi.1"); // password protected ap

  if(!res) {
      Serial.println("Failed to connect");
      // ESP.restart();
  } 
  else {
      //if you get here you have connected to the WiFi    
      Serial.println("connected...yeey :)");
  }
  // Set up mDNS responder:
  // - first argument is the domain name, in this example
  //   the fully-qualified domain name is "esp32.local"
  // - second argument is the IP address to advertise
  //   we send our IP address on the WiFi network
  if (!MDNS.begin("esp32dl")) {
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }

  Serial.println("mDNS responder started");

  MDNS.addService("http", "tcp", 80);

//#if SOC_WIFI_SUPPORTED || CONFIG_ESP_WIFI_REMOTE_ENABLED || LT_ARD_HAS_WIFI
  //WiFi.mode(WIFI_AP);
  //WiFi.softAP("esp_captive");
//#endif

  // curl -v http://192.168.4.1/
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/index.html");
  });
  // curl -v http://192.168.4.1/index.html
  server.serveStatic("/", SD, "/");
  server.serveStatic("/recordings", SD, "/recordings");

  preferences.begin("datalogger", false);
  float calvalue = preferences.getFloat("calvalue", 0);
  Serial.println("current calibration value:");
  Serial.println(calvalue);
  LoadCell.begin();
  
  //LoadCell.setReverseOutput(); //uncomment to turn a negative output value to positive
  unsigned long stabilizingtime = 2000; // preciscion right after power-up can be improved by adding a few seconds of stabilizing time
  boolean _tare = true; //set this to false if you don't want tare to be performed in the next step
  LoadCell.start(stabilizingtime, _tare);
  if (LoadCell.getTareTimeoutFlag() || LoadCell.getSignalTimeoutFlag()) {
    Serial.println("Timeout, check MCU>HX711 wiring and pin designations");
    while (1);
  }
  else {
    LoadCell.setCalFactor(calvalue); // user set calibration value (float), initial value 1.0 may be used for this sketch
    Serial.println("Startup is complete");
  }
  while (!LoadCell.update());

  server.on("/tare", HTTP_POST, tare);
  server.on("/hometare", HTTP_POST, hometare);
  server.on("/calibrate", HTTP_GET, calibrate);
  server.on("/start", HTTP_POST, start);
  server.on("/stop", HTTP_POST, stop);

  // Add this handler to your setup() in StaticFile.ino to serve the file list as JSON
  server.on("/list", HTTP_GET, [](AsyncWebServerRequest *request){
    String dir = "/";
    if (request->hasParam("dir")) dir = request->getParam("dir")->value();
    File root = SD.open(dir);
    if (!root || !root.isDirectory()) {
      request->send(404, "application/json", "[]");
      return;
    }
    String output = "[";
    File file = root.openNextFile();
    bool first = true;
    while (file) {
      if (!file.isDirectory()) {
        if (!first) output += ",";
        String fname = String(file.name());
        String url = fname;
        // Ensure the URL is correct for /recordings
        if (dir.endsWith("/")) {
          if (!fname.startsWith(dir)) url = dir + fname;
        } else {
          if (!fname.startsWith(dir + "/")) url = dir + "/" + fname;
        }
        output += "{\"name\":\"";
        output += fname.substring(fname.lastIndexOf('/') + 1);
        output += "\",\"url\":\"";
        output += url;
        output += "\"}";
        first = false;
      }
      file = root.openNextFile();
    }
    output += "]";
    request->send(200, "application/json", output);
  });

  server.begin();
  timeClient.begin();
}

// not needed
void loop() {
    timeClient.update();

    // Only read/loadcell and print if logging is active
    static boolean newDataReady = 0;
    const int serialPrintInterval = 0;
    static unsigned long t = 0;

    if (loggingActive) {
        if (LoadCell.update()) newDataReady = true;
        if (newDataReady) {
            if (millis() > t + serialPrintInterval) {
                float i = LoadCell.getData();
                Serial.print("Load_cell output val: ");
                Serial.println(i);
                // Save to buffer if space available
                if (sampleCount < MAX_SAMPLES) {
                    sampleBuffer[sampleCount].timestamp = millis();
                    sampleBuffer[sampleCount].value = i;
                    sampleCount++;
                }
                newDataReady = 0;
                t = millis();
            }
        }
    }

    delay(10);
}
