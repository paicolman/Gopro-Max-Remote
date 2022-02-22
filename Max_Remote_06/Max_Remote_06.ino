
/*
 * This version has the following functionality:
 * BUTTON 1: Start / Stop Video
 * BUTTON 2: Switch 360 / HERO mode
 * BUTTON 3: In 360 mode: Normal/TimeLapse / in HERO mode: Switch front / back camera
 * BUTTON 4: Highlight tag
 * 
 * ADDITIONAL: Reboot with BUTTON4 ON: Camera settings set back to normal and power off.
*/

#include <Arduino.h>
#include <ArduinoJson.h>
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

#define SDA 4
#define SCL 15

#define VIDEO_PIN 18
#define HERO_PIN 23
#define LENSE_PIN 19
#define HIGHLIGHT_PIN 22

struct Button {
    const uint8_t PIN;
    bool commandON;
};

Button button1 = {VIDEO_PIN, false};      // Video ON OFF
Button button2 = {HERO_PIN, false};       // 360 / HERO mode
Button button3 = {LENSE_PIN, false};      // front / back camera
Button button4 = {HIGHLIGHT_PIN, false};  // Highlight

bool buttonPressed = false;
bool filterPress = false;
uint8_t pinPressed = 0;
long loopCounter = 0;
bool abortAll = false;
bool isRecording = false;  // 0=OFF, 1=ON
bool isHeroActive = false;  // 0=360, 1=HERO
bool shootFront = true;     // 0=BACK, 1=FRONT lens
bool isLapse = false;     // 0=normal, 1=time lapse
bool turnOff = false;

WiFiClient client;
HTTPClient http;
SSD1306Wire display(0x3c, SDA, SCL);
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

bool connectWifi(bool disconnect) {
  bool connectedOk = false;
  display.clear();
  display.setFont(ArialMT_Plain_16);
  if (disconnect) {
    display.drawString(0, 0, "Disconnecting...");
  } else {
    display.drawString(0, 0, "Connecting...");
  }
  display.display();
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int retrials = 0;
  while (WiFi.status() != WL_CONNECTED && retrials < 20) {
    display.fillRect(10, 35, 10+retrials*5, 10);
    display.display();
    retrials++;
    // Serial.println("Trying...");
    delay(500);
  }
  // Serial.println("Ended retrial");
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
  digitalWrite(25, HIGH);
  bool ret = false;
  http.begin(client, urlCommand);
  //// Serial.printf("[HTTP] GET...%s\n", urlCommand.c_str());
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    //// Serial.println("Response ok:"+String(httpCode));
    String payload = http.getString();
    //// Serial.println("Response (str):"+payload);
    //// Serial.println(payload.length());
    ret = true;
  } else {
    //// Serial.printf("[HTTP] URL: %s\n", urlCommand.c_str());
    //// Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    //// Serial.printf("[HTTP] GET... error code: %d\n", httpCode);

    ret = false;
  }
  //delay(200);
  digitalWrite(25, LOW);
  return ret;
}

bool getCamInfo() {
  digitalWrite(25, HIGH);
  // Serial.println("Getting Cam Info");
  byte battLevel = 5;
  byte recording = 2;
  int camMode = 196608;
  byte videoMode = 3;
  byte lense = 3;
  http.begin(client, "http://10.5.5.9/gp/gpControl/status");
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String jsonPayload = http.getString();
    //// Serial.println(jsonPayload);
    int str_len = jsonPayload.length() + 1; 
    //// Serial.println(str_len);
    char jsonChar[str_len];
    jsonPayload.toCharArray(jsonChar, str_len);
    StaticJsonDocument<2600> doc;
    DeserializationError error = deserializeJson(doc, jsonChar);
    if (!error) {
      battLevel = doc["status"]["2"];
      recording = doc["status"]["10"];
      camMode   = doc["status"]["93"];
      videoMode = doc["status"]["89"];
      lense     = doc["settings"]["143"];
    } else {
      // Serial.print("Error decoding json: ");
      // Serial.println(error.c_str());
    }
  } else {
    // Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    // Serial.printf("[HTTP] GET... error code: %d\n", httpCode);
  }
  int battPercent = battLevel * 100 / 4;
  String battString = "BATT:"+String(battPercent);
  
  isRecording  = (recording == 1);
  isHeroActive = (camMode   == 1);
  shootFront   = (lense     == 0);
  String recString = "OFF";
  if (isRecording) {
    recString = "ON";
  }
  String modeString = " -360- :" + recString;
  if (videoMode == 24) {
    modeString = " LAPSE :" + recString;
    isLapse = true;
  }
  if (isHeroActive) {
    String shootString = " (B):";
    if (shootFront) {
      shootString = " (F):";
    }
    modeString = "HERO" + shootString + recString;
  }
  // Serial.printf("Batt  : %d\n",battLevel);
  // Serial.printf("Mode  : %d\n",camMode);
  // Serial.printf("Rec   : %d\n",recording);
  // Serial.printf("Video : %d\n",videoMode);
  // Serial.printf("Lense : %d\n",lense);

  if ((videoMode != 12)  && (videoMode != 24)) {
    return false;
  }

  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.drawString(0, 0, modeString);
  display.drawString(0, 20, battString);
  display.drawString(0, 40, " >>> READY <<<");
  display.display();
  digitalWrite(25, LOW);
  return true;
}

void camInfo() {
  bool info = false;
  while (!info) {
    info = getCamInfo();
  }
}

void turnOffCam() {
  if (camConnected) {
    display.clear();
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 0, "TURN CAM OFF");
    display.setFont(ArialMT_Plain_10);     
    display.drawString(0, 17, "- Power off: 5 min");
    display.display(); // Serial.println("Turning auto power OFF to: 5 min");
    sendCamCommand("http://10.5.5.9/gp/gpControl/setting/59/4");
    delay(500);
    display.drawString(0, 37, "- Turn cam OFF (sleep)");
    display.display();  // Serial.println("Turn cam OFF (sleep)");
    sendCamCommand("http://10.5.5.9/gp/gpControl/command/system/sleep");
    delay(500);
    display.clear();
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 0,  "- CAMERA OFF -");
    display.drawString(0, 20, "  DISCONNECT");
    display.drawString(0, 40, "     REMOTE");
    display.display();
  }
}

void setupCam() {
  if (camConnected) {
    display.clear();
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 0, "--SETUP CAM--");
    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 17, "- Power off: NEVER");
    display.display(); // Serial.println("Turning auto power OFF to: NEVER");
    sendCamCommand("http://10.5.5.9/gp/gpControl/setting/59/0");
    display.drawString(0, 27, "- Set Video mode: 360");
    display.display(); // Serial.println("Switch to Video mode");
    sendCamCommand("http://10.5.5.9/gp/gpControl/command/set_mode?p=1000");
    sendCamCommand("http://10.5.5.9/gp/gpControl/setting/142/1");
    display.drawString(0, 37, "- Locate: OFF");
    display.display();  // Serial.println("Locate OFF");
    sendCamCommand("http://10.5.5.9/gp/gpControl/command/system/locate?p=0");
    display.drawString(0, 47, "- Screensaver: 1m");
    display.display();  // Serial.println("Screensaver: 1 minute");
    sendCamCommand("http://10.5.5.9/gp/gpControl/setting/51/1");
    display.clear();
    display.drawString(0, 0, "- GPS: OFF");
    display.display();  // Serial.println("GPS OFF");
    sendCamCommand("http://10.5.5.9/gp/gpControl/setting/83/0");
    display.drawString(0, 10, "- Beep: LOW");
    display.display();  // Serial.println("Beep LOW");
    sendCamCommand("http://10.5.5.9/gp/gpControl/setting/87/40");
    display.drawString(0, 20, "- LCD: 10%");
    display.display();  // Serial.println("LCD to 10%");
    sendCamCommand("http://10.5.5.9/gp/gpControl/setting/88/10");
    display.drawString(0, 30, "- LEDs: ON");
    display.display();  // Serial.println("LEDs ON");
    sendCamCommand("http://10.5.5.9/gp/gpControl/setting/91/2");    
    //display.drawString(0, 40, "- LCD LOCK: ON");
    //display.display();  // Serial.println("LEDs ON");
    //sendCamCommand("http://10.5.5.9/gp/gpControl/setting/50/1");    
  }
}

void allButtonsOFF() {
  button1.commandON = false;
  button2.commandON = false;
  button3.commandON = false;
  button4.commandON = false;
}

void toggleCommand(bool isOn,  String onCmd, String offCmd, String onUrl, String offUrl) {
  if (isOn) {
    // Serial.println(onCmd);
    display.drawString(0, 20, onCmd);
    sendCamCommand(onUrl);
    display.display();
  } else {
    // Serial.println(offCmd);
    display.drawString(0, 20, offCmd);
    sendCamCommand(offUrl);
    display.display();
  }
}


void toggleButtonCmd(Button* butt) {
  display.setFont(ArialMT_Plain_16);
  
  switch(butt->PIN)  {
    case VIDEO_PIN:
      display.clear();
      display.drawString(0, 0, "VIDEO cmd:");
      display.display();
      toggleCommand(isRecording, " -> STOP", " -> START", "http://10.5.5.9/gp/gpControl/command/shutter?p=0", "http://10.5.5.9/gp/gpControl/command/shutter?p=1");
      break;
    case HERO_PIN:
      if (!isRecording) {
        display.clear();
        display.drawString(0, 0, "MODE cmd:");
        display.display();
        if (isLapse) {
          sendCamCommand("http://10.5.5.9/gp/gpControl/command/set_mode?p=1000");
        }
        toggleCommand(isHeroActive, " -> 360", " -> HERO", "http://10.5.5.9/gp/gpControl/setting/142/1", "http://10.5.5.9/gp/gpControl/setting/142/0"); 
      } else {
        display.clear();
        display.drawString(0, 0,  " >> IGNORED <<");
        display.drawString(0, 20, "    RECORDING");
        display.display();
      }      
      break;
    case LENSE_PIN:
      if (!isRecording) {
        if (isHeroActive) {
          display.clear();
          display.drawString(0, 0, "LENSE cmd:");
          display.display();
          toggleCommand(shootFront, " -> BACK", " -> FRONT", "http://10.5.5.9/gp/gpControl/setting/143/1", "http://10.5.5.9/gp/gpControl/setting/143/0");
        } else {
          display.clear();
          display.drawString(0, 0, "SPEED cmd:");
          display.display();
          toggleCommand(isLapse, " -> NORMAL", " -> LAPSE", "http://10.5.5.9/gp/gpControl/command/set_mode?p=1000", "http://10.5.5.9/gp/gpControl/command/set_mode?p=1002");          
        }
      } else {
        display.clear();
        display.drawString(0, 0,  " >> IGNORED <<");
        display.drawString(0, 20, "    RECORDING");
        display.display();
      }
      break;
    case HIGHLIGHT_PIN:
      //Highlight
      if (isRecording) {
        display.setFont(ArialMT_Plain_10);
        display.setColor(WHITE);
        display.drawString(0, 55, "****************************");
        display.display();
        sendCamCommand("http://10.5.5.9/gp/gpControl/command/storage/tag_moment");
        display.setColor(BLACK);
        display.drawString(0, 55, "****************************");
        display.display();
        display.setColor(WHITE);
        display.setFont(ArialMT_Plain_16);
      } else {
        display.clear();
        display.drawString(0, 0,  " >> IGNORED <<");
        display.drawString(0, 20, "NO RECORDING");
        display.display();
      }
      break;
  }
} 

void setup() {
   Serial.begin(115200);
   Serial.flush();
  //Reset OLED
  pinMode(16, OUTPUT);
  digitalWrite(16, HIGH);
  //LED
  pinMode(25, OUTPUT);
  digitalWrite(25, LOW);
  //Read button 4 to power off 
  pinMode(button4.PIN, INPUT);
  turnOff = digitalRead(button4.PIN) == LOW;
  display.init();
  display.flipScreenVertically();
  display.setBrightness(255);
  // Serial.println("");    
   Serial.println("************** START **************");
  pinMode(button1.PIN, INPUT);
  attachInterruptArg(button1.PIN, isr, &button1,  );
  pinMode(button2.PIN, INPUT);
  attachInterruptArg(button2.PIN, isr, &button2, HIGH);
  pinMode(button3.PIN, INPUT);
  attachInterruptArg(button3.PIN, isr, &button3, HIGH);
  
  attachInterruptArg(button4.PIN, isr, &button4, HIGH);
  
  camConnected = connectWifi(turnOff);
  abortAll = (!camConnected) || (turnOff);
  if (turnOff) {
    turnOffCam();
  } else if (camConnected) {
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
        case VIDEO_PIN:
          toggleButtonCmd(&button1);
          break;
        case HERO_PIN:
          toggleButtonCmd(&button2);
          break;
        case LENSE_PIN:
          toggleButtonCmd(&button3);
          break;
        case HIGHLIGHT_PIN:
          toggleButtonCmd(&button4);
          break;
        }
        buttonPressed = false;     
        delay(500);
        camInfo();
        filterPress = false;
      }
      if (loopCounter == 60000000) { //100000000 = about 20 sec.
        camInfo();
        loopCounter = 0;
      }
    }
    loopCounter++;
    if (WiFi.status() != WL_CONNECTED) {
      // Serial.println("Cam disconnected. Trying to connect again");
      camConnected = connectWifi(false);
    }
  }
}
