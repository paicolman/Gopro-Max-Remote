/*

This Version is to improve connectability

*/

#include <Arduino.h>
#include "ArduinoJson.h"

// For Wifi
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

//For display
#include <Wire.h>
#include "SSD1306Wire.h"

#ifndef STASSID
#define STASSID "Jorge MAX"
#define STAPSK  "4s3-xgN-pZs"
#endif

//Timing stuff
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;

WiFiClient client;
HTTPClient http;
SSD1306Wire display(0x3c, SDA, SCL);

//const String ssid = STASSID;
//const String password = STAPSK;
const char* ssid = STASSID;
const char* password = STAPSK;

bool camConnected = false;

bool connectWifi() {
  bool connectedOk = false;
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.drawString(0, 0, "Connecting...");
  display.display();
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int retrials = 0;
  while (WiFi.status() != WL_CONNECTED && retrials < 20) {
    display.fillRect(10, 35, 10+retrials*5, 10);
    display.display();
    retrials++;
    Serial.println("Trying...");
    delay(500);
  }
  Serial.println("Ended retrial");
  display.clear();
  if (WiFi.status() != WL_CONNECTED) {
    display.setFont(ArialMT_Plain_24);
    display.drawString(0, 5, "FAILED!");
    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 35, "Make sure the GoPRO WIFI");
    display.drawString(0, 50, "is ON and reset the remote");
    connectedOk = false;
  } else {
    connectedOk = true;
    display.drawString(0, 5, "CONNECTED!");
  }
  display.display();
  return connectedOk;
}


bool sendCamCommand(String urlCommand) {
  bool ret = false;
  http.begin(client, urlCommand);
  //Serial.print("[HTTP] GET...\n");
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    //Serial.println("Response ok:"+String(httpCode));
    String payload = http.getString();
    //Serial.println("Response (str):"+payload);
    //Serial.println(payload.length());
    ret = true;
  } else {
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
      Serial.printf("[HTTP] GET... error code: %d\n", httpCode);
      display.clear();
      display.setFont(ArialMT_Plain_24);
      display.drawString(0, 5, "CAM LOST!");
      display.setFont(ArialMT_Plain_16);
      display.drawString(0, 32, "Please restart...");
      display.display();
      ret = false;
  }
  return ret;
}

byte getBatteryInfo() {
  byte batt = 5;
  http.begin(client, "http://10.5.5.9/gp/gpControl/status");
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String jsonPayload = http.getString();
    //Serial.println(jsonPayload);
    int str_len = jsonPayload.length() + 1; 
    //Serial.println(str_len);
    char jsonChar[str_len];
    jsonPayload.toCharArray(jsonChar, str_len);
    StaticJsonDocument<2600> doc;
    DeserializationError error = deserializeJson(doc, jsonChar);
    if (!error) {
      //Serial.println("Decoding json OK!!");
      StaticJsonDocument<2600> params = doc["status"];
      batt = params["2"];
      //Serial.printf("BATTERY LEVEL: %d\n", batt);
    } else {
      Serial.println("Error decoding json");
      Serial.println(error.c_str());
    }
  }
  return batt;
}

String getTime() {
  unsigned long time = millis();
  unsigned long hours = time/1000/60/60;
  unsigned long minutes = time/1000/60 - 60 * hours;
  unsigned long seconds = time/1000 - 60 * minutes;

  return String(String(hours) +":"+ String(minutes) +":"+ String(seconds));
}

void setupCam() {
  Serial.println("Turning auto power OFF to: NEVER");
  sendCamCommand("http://10.5.5.9/gp/gpControl/setting/59/0");
  delay(1000);
  Serial.println("Switch to Video mode");
  sendCamCommand("http://10.5.5.9/gp/gpControl/command/set_mode?p=1000");
  delay(1000);
  Serial.println("Locate OFF");
  sendCamCommand("http://10.5.5.9/gp/gpControl/command/system/locate?p=0");
  delay(1000);
  Serial.println("Screensaver: 1 minute");
  sendCamCommand("http://10.5.5.9/gp/gpControl/setting/51/1");
  delay(1000);
  Serial.println("GPS OFF");
  sendCamCommand("http://10.5.5.9/gp/gpControl/setting/83/0");
  delay(1000);
  Serial.println("Beep OFF");
  sendCamCommand("http://10.5.5.9/gp/gpControl/setting/87/0");
  delay(1000);
  Serial.println("LCD to 10%");
  sendCamCommand("http://10.5.5.9/gp/gpControl/setting/88/10");
  delay(1000);
  Serial.println("LEDs ON");
  sendCamCommand("http://10.5.5.9/gp/gpControl/setting/91/2");
}


void setup() {
  Serial.begin(115200);
  Serial.flush();
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  Serial.println("Connecting to WiFi");
  camConnected = connectWifi();
  Serial.printf("Connected??? %d\n", camConnected);
  delay(1000);
  //byte batt = getBatteryInfo();
  if (camConnected) {
    Serial.printf("%s - Starting...\n", getTime());
    setupCam();
  } else {
    Serial.printf("%s - ABORTED! Cam not connected\n", getTime());
  }
}

int loopCount = 1;
bool notWarned = true;

void loop() {
  // put your main code here, to run repeatedly:
  if (camConnected) {
    Serial.println("Waiting 5 minutes...");
    delay(300000); //Wait 5 minutes
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Cam disconnected. Trying to connect again");
      camConnected = connectWifi();
    } else {
      byte batt = getBatteryInfo();
      Serial.printf("%s - Battery level:%d\n", getTime(), batt);
  
      //Record for 30 seconds and stop
      Serial.printf("%s - Recording loop:%d\n", getTime(), loopCount);
      sendCamCommand("http://10.5.5.9/gp/gpControl/command/shutter?p=1");
      delay(30000);
      sendCamCommand("http://10.5.5.9/gp/gpControl/command/shutter?p=0");
      loopCount++;
    }
  } else {
    if (notWarned) {
      notWarned = false;
      Serial.println("Cam disconnected for good. Doing nothing else here...");
    }
  }

}
