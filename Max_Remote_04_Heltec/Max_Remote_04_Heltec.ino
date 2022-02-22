#include <Arduino.h>
#include <ArduinoJson.h>
// For Wifi
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

//For display
#include <Wire.h>
#include "heltec.h"

#ifndef STASSID
#define STASSID "Jorge MAX"
#define STAPSK  "4s3-xgN-pZs"
#endif

#define SDA 4
#define SCL 15
#define SD1306RST 16 //RST must be set by software

struct Button {
    const uint8_t PIN;
    bool commandON;
};

Button button1 = {36, false}; // Video ON OFF
Button button2 = {39, false}; // Time Lapse ON OFF
Button button3 = {34, false}; // Highlight
Button button4 = {35, false}; // RESET

bool buttonPressed = false;
bool filterPress = false;
uint8_t pinPressed = 0;
long loopCounter = 0;
bool abortAll = false;

WiFiClient client;
HTTPClient http;
const char* ssid = STASSID;
const char* password = STAPSK;
bool camConnected = false;


void IRAM_ATTR isr(void* arg) {
  if (!filterPress) {
    buttonPressed = true;
    Button* s = static_cast<Button*>(arg);
    s->commandON = !s->commandON;
    pinPressed = s->PIN;
  }
}

bool connectWifi() {
  bool connectedOk = false;
  Heltec.display->clear();
  Heltec.display->setFont(ArialMT_Plain_16);
  Heltec.display->drawString(0, 0, "Connecting...");
  Heltec.display->display();
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int retrials = 0;
  while (WiFi.status() != WL_CONNECTED && retrials < 20) {
    Heltec.display->fillRect(10, 35, 10+retrials*5, 10);
    Heltec.display->display();
    retrials++;
    Serial.println("Trying...");
    delay(500);
  }
  Serial.println("Ended retrial");
  Heltec.display->clear();
  if (WiFi.status() != WL_CONNECTED) {
    Heltec.display->setFont(ArialMT_Plain_24);
    Heltec.display->drawString(0, 5, "FAILED!");
    Heltec.display->setFont(ArialMT_Plain_10);
    Heltec.display->drawString(0, 35, "Make sure the GoPRO WIFI");
    Heltec.display->drawString(0, 50, "is ON and reset the remote");
    connectedOk = false;
  } else {
    connectedOk = true;
    Heltec.display->drawString(0, 5, "CONNECTED!");
  }
  Heltec.display->display();
  return connectedOk;
}

bool sendCamCommand(String urlCommand) {
  bool ret = false;
  http.begin(client, urlCommand);
  //Serial.printf("[HTTP] GET...%s\n", urlCommand.c_str());
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    //Serial.println("Response ok:"+String(httpCode));
    String payload = http.getString();
    //Serial.println("Response (str):"+payload);
    //Serial.println(payload.length());
    ret = true;
  } else {
    //Serial.printf("[HTTP] URL: %s\n", urlCommand.c_str());
    //Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    //Serial.printf("[HTTP] GET... error code: %d\n", httpCode);

    ret = false;
  }
  delay(1000);
  return ret;
}

bool getCamInfo() {
  Serial.println("Getting Cam Info");
  byte battLevel = 5;
  byte recording = 2;
  byte camMode = 3;
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
      battLevel = params["2"];
      recording = params["10"];
      camMode   = params["89"];
      //Serial.printf("BATTERY LEVEL: %d\n", batt);
    } else {
      Serial.print("Error decoding json: ");
      Serial.println(error.c_str());
    }
  } else {
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    Serial.printf("[HTTP] GET... error code: %d\n", httpCode);
  }
  int battPercent = battLevel * 100 / 4;
  String battString = "BATT:"+String(battPercent);
  String recString = "ON";
  if (recording == 0) {
    recString = "OFF";
  }
  String modeString = "VIDEO:"+recString;
  if (camMode == 24) {
    modeString = "LAPSE:"+recString;
  }
  Serial.printf("Batt: %d\n",battLevel);
  Serial.printf("Mode: %d\n",camMode);
  Serial.printf("Rec : %d\n",recording);

  if ((camMode != 12) && (camMode != 24)) {
    return false;
  }

  button1.commandON = (camMode == 12) && (recording == 1);
  button2.commandON = (camMode == 24) && (recording == 1);

  Heltec.display->clear();
  Heltec.display->setFont(ArialMT_Plain_16);
  Heltec.display->drawString(0, 0, modeString);
  Heltec.display->drawString(0, 20, battString);
  Heltec.display->drawString(0, 40, " -- READY --");
  Heltec.display->display();
  return true;
}

void camInfo() {
  bool info = false;
  while (!info) {
    info = getCamInfo();
  }
}

void setupCam() {
  if (camConnected) {
    Heltec.display->clear();
    Heltec.display->setFont(ArialMT_Plain_16);
    Heltec.display->drawString(0, 0, "--SETUP CAM--");
    Heltec.display->setFont(ArialMT_Plain_10);
    Heltec.display->drawString(0, 17, "- Power off: NEVER");
    Heltec.display->display();
    Serial.println("Turning auto power OFF to: NEVER");
    sendCamCommand("http://10.5.5.9/gp/gpControl/setting/59/0");
    Heltec.display->drawString(0, 27, "- Set Video mode");
    Heltec.display->display();
    Serial.println("Switch to Video mode");
    sendCamCommand("http://10.5.5.9/gp/gpControl/command/set_mode?p=1000");
    Heltec.display->drawString(0, 37, "- Locate: OFF");
    Heltec.display->display();  Serial.println("Locate OFF");
    sendCamCommand("http://10.5.5.9/gp/gpControl/command/system/locate?p=0");
    Heltec.display->drawString(0, 47, "- Screensaver: 1m");
    Heltec.display->display();  Serial.println("Screensaver: 1 minute");
    sendCamCommand("http://10.5.5.9/gp/gpControl/setting/51/1");
    Heltec.display->clear();
    Heltec.display->drawString(0, 0, "- GPS: OFF");
    Heltec.display->display();  Serial.println("GPS OFF");
    sendCamCommand("http://10.5.5.9/gp/gpControl/setting/83/0");
    Heltec.display->drawString(0, 10, "- Beep: LOW");
    Heltec.display->display();  Serial.println("Beep LOW");
    sendCamCommand("http://10.5.5.9/gp/gpControl/setting/87/40");
    Heltec.display->drawString(0, 20, "- LCD: 10%");
    Heltec.display->display();  Serial.println("LCD to 10%");
    sendCamCommand("http://10.5.5.9/gp/gpControl/setting/88/10");
    Heltec.display->drawString(0, 30, "- LEDs: ON");
    Heltec.display->display();  Serial.println("LEDs ON");
    sendCamCommand("http://10.5.5.9/gp/gpControl/setting/91/2");    
  }
}

void allButtonsOFF() {
  button1.commandON = false;
  button2.commandON = false;
  button3.commandON = false;
  button4.commandON = false;
}

void toggleCommand(bool isOn, String onCmd, String offCmd) {
  if (isOn) {
    Serial.println(onCmd);
    Heltec.display->drawString(0, 20, "-->  ON");
    Heltec.display->display();
    sendCamCommand("http://10.5.5.9/gp/gpControl/command/shutter?p=1");
  } else {
    Serial.println(offCmd);
    Heltec.display->drawString(0, 20, "--> OFF");
    Heltec.display->display();
    sendCamCommand("http://10.5.5.9/gp/gpControl/command/shutter?p=0");
  }
}

void toggleButtonCmd(Button* butt) {
  Heltec.display->setFont(ArialMT_Plain_16);

  Serial.printf("--> VIDEO: %d\n", button1.commandON);
  Serial.printf("--> LAPSE: %d\n", button2.commandON);
  
  switch(butt->PIN)  {
    case 36:
      //Switch to video mode
      Heltec.display->clear();
      Heltec.display->drawString(0, 0, "VIDEO cmd:");
      Heltec.display->display();
      if(button2.commandON) {
        sendCamCommand("http://10.5.5.9/gp/gpControl/command/shutter?p=0");        
      }
      sendCamCommand("http://10.5.5.9/gp/gpControl/command/set_mode?p=1000");
      toggleCommand(butt->commandON, " --> VIDEO ON", " --> VIDEO OFF");
      break;
    case 39:
      //Switch to TimeLapse mode
      Heltec.display->clear();
      Heltec.display->drawString(0, 0, "LAPSE cmd:");
      Heltec.display->display();
      if(button1.commandON) {
        sendCamCommand("http://10.5.5.9/gp/gpControl/command/shutter?p=0");        
      }
      sendCamCommand("http://10.5.5.9/gp/gpControl/command/set_mode?p=1002");
      toggleCommand(butt->commandON, " --> LAPSE ON", " --> LAPSE OFF");
      break;
    case 34:
      //Highlight
      if ((button1.commandON) || (button2.commandON)) {
        Heltec.display->setFont(ArialMT_Plain_10);
        Heltec.display->setColor(WHITE);
        Heltec.display->drawString(0, 55, "**************************");
        Heltec.display->display();
        sendCamCommand("http://10.5.5.9/gp/gpControl/command/storage/tag_moment");
        Heltec.display->setColor(BLACK);
        Heltec.display->drawString(0, 55, "**************************");
        Heltec.display->display();
        Heltec.display->setColor(WHITE);
        Heltec.display->setFont(ArialMT_Plain_16);
      }
      break;
  }
}

void(* resetFunc) (void) = 0; 

void setup() {
  Heltec.begin(true /*DisplayEnable Enable*/, false /*LoRa Disable*/, true /*Serial Enable*/);
  
  Serial.begin(115200);
  Serial.flush();
  Heltec.display->init();
  Heltec.display->flipScreenVertically();
  Serial.println("");    
  Serial.println("************** START **************");
  pinMode(button1.PIN, INPUT);
  attachInterruptArg(button1.PIN, isr, &button1, HIGH);
  pinMode(button2.PIN, INPUT);
  attachInterruptArg(button2.PIN, isr, &button2, HIGH);
  pinMode(button3.PIN, INPUT);
  attachInterruptArg(button3.PIN, isr, &button3, FALLING);
  pinMode(button4.PIN, INPUT);
  attachInterruptArg(button4.PIN, isr, &button4, CHANGE);
  camConnected = connectWifi();
  abortAll = !camConnected;
  if (camConnected) {
    setupCam();
    buttonPressed = false;
    camInfo();
  }
}

void loop() {
  if (!abortAll) {
    if (camConnected) {
      if (buttonPressed) {
        filterPress = true;
        switch(pinPressed) {
        case 36:
          toggleButtonCmd(&button1);
          break;
        case 39:
          toggleButtonCmd(&button2);
          break;
        case 34:
          toggleButtonCmd(&button3);
          break;
        case 35:
          resetFunc(); //Force reset if all else fails!
          break;
        }
        buttonPressed = false;
        camInfo();
        delay(500);
        filterPress = false;
      }
      if (loopCounter == 300000000) { //100000000 = about 20 sec.
        camInfo();
        loopCounter = 0;
      }
    }
    loopCounter++;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Cam disconnected. Trying to connect again");
      camConnected = connectWifi();
    }
  } else {
    if (pinPressed == 35) {
      resetFunc(); //Force reset
    }
  }
}
