
/*
 * This version has the following functionality:
 * BUTTON 1: Start / Stop Video
 * BUTTON 2: Switch 360 / HERO mode
 * BUTTON 3: In 360 mode: Normal/TimeLapse / in HERO mode: Switch front / back camera
 * BUTTON 4: Highlight tag
 * 
 * ADDITIONAL: Reboot with BUTTON4 ON: Camera settings set back to normal and power off.
 * 
 * IMPROVEMENTS:
 * - Reset with BUTTON4 on: Remote performs settings in the camera and disconnects from it
 * - The auto cam check is now done every minute, ussing millis()
 * - The auto cam check is triggered after every command
 * - Reset with BUTTON3 on: Remote enters in automatic mode, recording clips of 2 minutes every 5 minutes in 360 mode.
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

#include "images.h"

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

#define STARTCMD 1
#define STOPCMD  2
#define MAXCMD   3
#define HEROCMD  4
#define NORMCMD  5
#define LAPSECMD 6
#define FRONTCMD 7
#define BACKCMD  8


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
bool autoMode = false;
int autoStopCounter = -1;
int autoRecCounter = 0;

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
  int camMode = 3;
  byte videoMode = 3;
  byte lense = 3;
  http.begin(client, "http://10.5.5.9/gp/gpControl/status");
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String jsonPayload = http.getString();
    //Serial.println(jsonPayload);
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
  isRecording  = (recording == 1);
  isHeroActive = (camMode   == 0);
  shootFront   = (lense     == 0);

  Serial.printf("Batt  : %d\n",battLevel);
  Serial.printf("Mode  : %d\n",camMode);
  Serial.printf("Rec   : %d\n",recording);
  Serial.printf("Video : %d\n",videoMode);
  Serial.printf("Lense : %d\n",lense);

  if ((videoMode != 12)  && (videoMode != 24)) {
    return false;
  }

  display.clear();
  if(!autoMode) {
    if (isHeroActive) {
      display.drawXbm(0, 0, genericLabel_width, genericLabel_height, heroLabel);
      if(shootFront) {
        display.drawXbm(0, 24, genericLabel_width, genericLabel_height, front);
      }else{
        display.drawXbm(0, 24, genericLabel_width, genericLabel_height, back);
      }
    }else{
      display.drawXbm(0, 0, genericLabel_width, genericLabel_height, maxLabel);
      if (videoMode == 24) {
        display.drawXbm(0, 24, genericLabel_width, genericLabel_height, lapse);
        isLapse = true;
      }else{
        display.drawXbm(0, 24, genericLabel_width, genericLabel_height, normal);
        isLapse = false;
      }
    }
    if (isRecording) {
      display.drawXbm(75, 0, onoffLabel_width, onoffLabel_height, onLabel);
    }else{
      display.drawXbm(75, 0, onoffLabel_width, onoffLabel_height, offLabel);
    }
  } else {
    if (isRecording) {
      display.drawXbm(0, 6, automode_width, automode_height, autorec);
    }else{
      display.drawXbm(0, 6, automode_width, automode_height, automode);
    }
  }

  switch(battLevel) {
    case 0:
      display.drawXbm(0, 48, info_width, info_height, batt000);
      break;
    case 1:
      display.drawXbm(0, 48, info_width, info_height, batt025);
      break;
    case 2:
      display.drawXbm(0, 48, info_width, info_height, batt050);
      break;
    case 3:
      display.drawXbm(0, 48, info_width, info_height, batt075);
      break;
    case 4:
      display.drawXbm(0, 48, info_width, info_height, batt100);
      break;
    case 5:
      display.drawXbm(0, 48, info_width, info_height, batt100);
      break;
  }
  
  //display.setFont(ArialMT_Plain_16);
  //display.drawString(0, 0, modeString);
  //display.drawString(0, 20, battString);
  //display.drawString(0, 40, " >>> READY <<<");
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
    camInfo();
  }
}

void setupAutoMode() {
    display.clear();
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 0, "--AUTO MODE--");
    display.drawString(0, 30, "Video: 360");
    display.display(); // Serial.println("Switch to Video mode");
    sendCamCommand("http://10.5.5.9/gp/gpControl/command/set_mode?p=1000");
    sendCamCommand("http://10.5.5.9/gp/gpControl/setting/142/1");
    camInfo();
}

void allButtonsOFF() {
  button1.commandON = false;
  button2.commandON = false;
  button3.commandON = false;
  button4.commandON = false;
}

// BITMAP BASED DISPLAY COMMANDS

void onOff(String onOffCmd) {
  
}

void toggleCommand(bool isOn,  int onCmd, int offCmd, String onUrl, String offUrl) {
  if (isOn) {
    Serial.println(onCmd);
    switch(onCmd) {
      case STOPCMD:
        display.drawXbm(0, 20, command_width, command_height, stopCmd);
        break;
      case MAXCMD:
        display.drawXbm(0, 20, command_width, command_height, maxCmd);
        break;
      case BACKCMD:
        display.drawXbm(0, 20, command_width, command_height, backCmd);
        break;
      case NORMCMD:
        display.drawXbm(0, 20, command_width, command_height, normCmd);
        break;
    }
    sendCamCommand(onUrl);
    display.display();
  } else {
    Serial.println(offCmd);
    switch(offCmd) {
      case STARTCMD:
        display.drawXbm(0, 20, command_width, command_height, startCmd);
        break;
      case HEROCMD:
        display.drawXbm(0, 20, command_width, command_height, heroCmd);
        break;
      case FRONTCMD:
        display.drawXbm(0, 20, command_width, command_height, frontCmd);
        break;
      case LAPSECMD:
        display.drawXbm(0, 20, command_width, command_height, lapseCmd);
        break;
    }
    sendCamCommand(offUrl);
    display.display();
  }
  camInfo();
}


void toggleButtonCmd(Button* butt) {
  display.setFont(ArialMT_Plain_16);
  
  switch(butt->PIN)  {
    case VIDEO_PIN:
      display.clear();
      display.drawXbm(0, 0, command_width, command_height, videoCmd);
      display.drawXbm(0, 40, command_width, command_height, wait);
      display.display();
      toggleCommand(isRecording, STOPCMD, STARTCMD, "http://10.5.5.9/gp/gpControl/command/shutter?p=0", "http://10.5.5.9/gp/gpControl/command/shutter?p=1");
      break;
    case HERO_PIN:
      if (!isRecording) {
        display.clear();
        display.drawXbm(0, 0, command_width, command_height, modeCmd);
      display.drawXbm(0, 40, command_width, command_height, wait);
        display.display();
        if (isLapse) {
          sendCamCommand("http://10.5.5.9/gp/gpControl/command/set_mode?p=1000");
        }
        toggleCommand(isHeroActive, MAXCMD, HEROCMD, "http://10.5.5.9/gp/gpControl/setting/142/1", "http://10.5.5.9/gp/gpControl/setting/142/0"); 
      } else {
        display.clear();
        display.drawXbm(0, 6, ignored_width, ignored_height, ignored);
        display.display();
      }      
      break;
    case LENSE_PIN:
      if (!isRecording) {
        if (isHeroActive) {
          display.clear();
          display.drawXbm(0, 0, command_width, command_height, lenseCmd);
          display.drawXbm(0, 40, command_width, command_height, wait);
          display.display();
          toggleCommand(shootFront, BACKCMD, FRONTCMD, "http://10.5.5.9/gp/gpControl/setting/143/1", "http://10.5.5.9/gp/gpControl/setting/143/0");
        } else {
          display.clear();
          display.drawXbm(0, 0, command_width, command_height, speedCmd);
          display.drawXbm(0, 40, command_width, command_height, wait);
          display.display();
          toggleCommand(isLapse, NORMCMD, LAPSECMD, "http://10.5.5.9/gp/gpControl/command/set_mode?p=1000", "http://10.5.5.9/gp/gpControl/command/set_mode?p=1002");          
        }
      } else {
        display.clear();
        display.drawXbm(0, 6, ignored_width, ignored_height, ignored);
        display.display();
      }
      break;
    case HIGHLIGHT_PIN:
      //Highlight
      if (isRecording) {
        display.setColor(WHITE);
        display.drawXbm(70, 48, info_width, info_height, highlight);
        display.display();
        sendCamCommand("http://10.5.5.9/gp/gpControl/command/storage/tag_moment");
        display.setColor(BLACK);
        display.drawXbm(70, 48, info_width, info_height, highlight);
        display.display();
        display.setColor(WHITE);
      } else {
        display.clear();
        display.drawXbm(0, 6, ignored_width, ignored_height, ignored);
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
  pinMode(button3.PIN, INPUT);
  pinMode(button4.PIN, INPUT);
  pinMode(button1.PIN, INPUT);
  pinMode(button2.PIN, INPUT);
  turnOff = digitalRead(button4.PIN) == LOW;
  autoMode = digitalRead(button3.PIN) == LOW;
  display.init();
  display.flipScreenVertically();
  display.setBrightness(255);
  // Serial.println("");    
  Serial.println("************** START **************");
  attachInterruptArg(button1.PIN, isr, &button1,  HIGH);  
  attachInterruptArg(button2.PIN, isr, &button2, HIGH);    
  attachInterruptArg(button3.PIN, isr, &button3, HIGH);
  attachInterruptArg(button4.PIN, isr, &button4, HIGH);
  
  camConnected = connectWifi(turnOff);
  abortAll = (!camConnected) || (turnOff);
  if (turnOff) {
    turnOffCam();
  }
  if ((autoMode) && (camConnected)) {
    setupAutoMode(); 
  }
  if ((!turnOff) && (!autoMode) && (camConnected)) {
    setupCam();
    buttonPressed = false;
    camInfo();
  }
}

void loop() {
  if (!abortAll) {
    if (camConnected && !autoMode) {
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
      if ((millis() - loopCounter) >= 60000) { //Every minute.
        Serial.printf("%s - Autocheck Cam status\n", String(millis()/1000));
        camInfo();
        loopCounter = millis();
      }
    }
    if (autoMode) {
      if (buttonPressed) {
        if(pinPressed == VIDEO_PIN) {
          autoMode = false;
          camInfo();
        }
      }
      if ((millis() - loopCounter) >= 60000) { //Every minute.
        Serial.printf("%s - Autocheck Cam status\n", String(millis()/1000));
        camInfo();
        loopCounter = millis();
        if (autoRecCounter >= 0) {
          autoRecCounter ++;
        }
        if (autoStopCounter >= 0) {
          autoStopCounter ++;
        }
      }      
      if (autoRecCounter >= 3) {
        autoRecCounter = -1;
        autoStopCounter = 0;
        Serial.printf("%s - Starting Video sequence\n", String(millis()/1000));
        display.clear();
        display.drawXbm(0, 0, command_width, command_height, videoCmd);
        display.drawXbm(0, 40, command_width, command_height, wait);
        display.display();
        toggleCommand(false, STOPCMD, STARTCMD, "http://10.5.5.9/gp/gpControl/command/shutter?p=0", "http://10.5.5.9/gp/gpControl/command/shutter?p=1");
      }
      if (autoStopCounter >= 1) {
        autoRecCounter = 0;
        autoStopCounter = -1;
        Serial.printf("%s - STOPPING Video sequence\n", String(millis()/1000));
        display.clear();
        display.drawXbm(0, 0, command_width, command_height, videoCmd);
        display.drawXbm(0, 40, command_width, command_height, wait);
        display.display();
        toggleCommand(true, STOPCMD, STARTCMD, "http://10.5.5.9/gp/gpControl/command/shutter?p=0", "http://10.5.5.9/gp/gpControl/command/shutter?p=1");
      }
    }
    if (WiFi.status() != WL_CONNECTED) {
      // Serial.println("Cam disconnected. Trying to connect again");
      camConnected = connectWifi(false);
    }
  }
}
