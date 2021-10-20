#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "BluetoothSerial.h"

Preferences preferences;
WiFiServer server(8080);
WiFiClient remoteClient;

const String apMacAddr = WiFi.macAddress();
const uint8_t lastIndexOfColon = apMacAddr.lastIndexOf(":");
const String apSsidStr = String("esp32_SR" + apMacAddr.substring(lastIndexOfColon));
const char* apSsid = apSsidStr.c_str();
const char* apPass = "password";

const bool consolePrint = true;

// incoming packets: 1B len of the entire packet including the length itself, 1B operation code, 2B empty in case of future use, rest data
// outgoing packets: 1B operation code of the request, 1B of result (0b00000000: no err and no data, 0b11111111: no err with data, else error code), ?data
// struct Packet_t {
//   uint8_t len;
//   byte emptyForLaterUse[2];
//   uint8_t op;
//   byte data[];
// };

// Packet_t* createPacket() {
//   Packet_t* packetTemp = (Packet_t*)malloc(sizeof(packetTemp->len));
//   packetTemp->len=0;
//   return packetTemp;
// }

// Packet_t* packet = createPacket();

String commandOP = "";
String commandInput = "";

const String OP_RESET = "reset";
const String OP_SET_ST_SSID = "ssid";
const String OP_SET_ST_PASS = "pass";
const String OP_CONNECT_TO_ST = "connect";
const String OP_DISABLE_AP = "disap";
const String OP_STATUS = "status";
const String OP_SCAN = "scan";

const uint8_t RES_CODE_ERR_404 = 0b00000001;
const uint8_t RES_CODE_ERR_WIFI_FAILED = 0b00000010;

const char* ST_SSID_KEY = "stSsid";
const char* ST_PASS_KEY = "stPass";

void printArray(byte *input, int len){
  int i;
  for(i=0; i< len; i++){
    Serial.print("0x");
    if(input[i]<16) Serial.print(0);
    Serial.print(input[i], HEX);
    Serial.print(" ");
  }
  Serial.println("z");
}

void reconnectSt(WiFiEvent_t event, WiFiEventInfo_t info) {
  String stSsid = preferences.getString(ST_SSID_KEY);
  if(stSsid.length()) {
    String stPass = preferences.getString(ST_PASS_KEY);
    WiFi.begin(stSsid.c_str(), stPass.c_str());
  }
}

bool connectST(String stSsid, String stPass, wifi_mode_t mode) {
  WiFi.mode(mode);
  WiFi.begin(stSsid.c_str(), stPass.c_str());

  int waitForConnect = 0;
  while(WiFi.status() != WL_CONNECTED){
    if(waitForConnect >= 600) {
      return false;
    }
    waitForConnect++;
    delay(100);
  }

  WiFi.onEvent(reconnectSt, SYSTEM_EVENT_STA_DISCONNECTED);
  return true;
}

void checkForConnections() {
  if (server.hasClient()) {
    if (remoteClient.connected()) {
      Serial.println("Connection rejected");
      server.available().stop();
    } else {
      Serial.println("Connection accepted");
      remoteClient = server.available();
    }
  }
}

bool readFromSocket() {
  if (remoteClient.connected() && remoteClient.available()) {
    String inputCommand = remoteClient.readStringUntil(0x0D);
    int indexOfSpace = inputCommand.indexOf(" ");

    if(indexOfSpace < 0) {
      commandOP = String(inputCommand);
      commandInput = "";
      return true;
    } else {
      commandOP = inputCommand.substring(0, indexOfSpace);
      commandInput = inputCommand.substring(indexOfSpace);
      return true;
    }
  }
  return false;
}

void resData(String data) {
  uint len = data.length()+1;
  char dataArr[len];
  data.toCharArray(dataArr, len);
  remoteClient.write((uint8_t*)dataArr, len);
}

void act() {
  if(commandOP == OP_SET_ST_SSID) {
    Serial.print("ssid: ");
    Serial.println(commandInput);
    preferences.putString(ST_SSID_KEY, commandInput);
    String output = "{\"code\":0, \"op\":\"" + commandOP;
    output += "\"}";
    resData(output);

  } else if (commandOP == OP_SET_ST_PASS) {
    Serial.print("pass: ");
    Serial.println(commandInput);
    preferences.putString(ST_PASS_KEY, commandInput);
    String output = "{\"code\":0, \"op\":\"" + commandOP;
    output += "\"}";
    resData(output);

  } else if (commandOP == OP_CONNECT_TO_ST) {
    bool connected = connectST(preferences.getString(ST_SSID_KEY), preferences.getString(ST_PASS_KEY), WIFI_AP_STA);
    if(!connected) {
      String output = "{\"code\":" + RES_CODE_ERR_WIFI_FAILED;
      output += "}";
      resData(output);
    } else {
      String ip = WiFi.localIP().toString();
      String output = "{\"code\":0,\"localIP\":\"" + ip;
      output += "\"}";
      resData(output);
    }

  } else if (commandOP == OP_RESET) {
    String output = "{\"code\":0,\"op\":\"" + commandOP;
    output += "\"}";
    resData(output);
    ESP.restart();

  } else if (commandOP == OP_DISABLE_AP) {
    String output = "{\"code\":0,\"op\":\"" + commandOP;
    output += "\"}";
    // TODO make sure it does not disconnect the wifi from station.
    WiFi.mode(WIFI_STA);

  } else if (commandOP == OP_STATUS) {
    String status = "{\"code\":0,\"op\":\"" + commandOP;
    status += "\",\"wifi\":";
    status.concat((int)WiFi.status());
    status += ",\"savedSSID\":\"" + preferences.getString(ST_SSID_KEY);
    status += "\",\"gatewayIP\":\"" + WiFi.gatewayIP();
    status += "\",\"localIP\":\"" + WiFi.localIP();
    status += "\"}";
    resData(status);

  } else if (commandOP == OP_SCAN) {
    int n = WiFi.scanNetworks();
    String output = "{\"code\":0,\"op\":\"" + commandOP;
    output += "\",\"networks\":[";
    for (int i = 0; i < n; ++i) {
      output += "{\"ssid\":\"";
      output += WiFi.SSID(i);
      output += "\",\"rssi\":";
      output += WiFi.RSSI(i);
      output += ",\"enc\":";
      output += WiFi.encryptionType(i);
      output += "}";
      if(i < n-1){
        output += ",";
      }
    }
    output += "]}";
    Serial.println("result: ===============");
    Serial.println(output);
    resData(output);
  } else {
    String output = "{\"code\":" + RES_CODE_ERR_404;
    output += (",\"op\":\"" + commandOP + "}");
    resData(output);
  }
}

bool createAPAndST() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid, apPass);
  Serial.print("AP started: ");
  Serial.println(WiFi.softAPIP());
  return true;
}

void startNetwork() {
  String stSsid = preferences.getString(ST_SSID_KEY);
  if(stSsid.length()) {
    String stPass = preferences.getString(ST_PASS_KEY);
    if(!connectST(stSsid, stPass, WIFI_STA)) {
      createAPAndST();
    }
  } else {
    createAPAndST();
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  preferences.begin("sr", false); 

  Serial.println(preferences.getString(ST_SSID_KEY));
  Serial.println(preferences.getString(ST_PASS_KEY));
  startNetwork();
  Serial.println("Before server.begin()");
  server.begin();



  // if(consolePrint) {
  //   Serial.println("\n[*] Creating AP");
  // }
  // WiFi.mode(WIFI_AP);
  // WiFi.softAP(ssid, password);
  // if(consolePrint) {
  //   Serial.print("[+] AP Created with IP Gateway ");
  //   Serial.println(WiFi.softAPIP());
  // }

  // Serial.begin(115200);
  // delay(1000);
  // WiFi.mode(WIFI_STA); //Optional
  // WiFi.begin(ssid, password);

  // if(consolePrint) {
  //   Serial.println("\nConnecting");
  //   while(WiFi.status() != WL_CONNECTED){
  //     Serial.print(".");
  //     delay(100);
  //   }
    
  //   Serial.println("\nConnected to the WiFi network");
  //   Serial.print("Local ESP32 IP: ");
  //   Serial.println(WiFi.localIP());
  // }
}

void loop() {
  checkForConnections();
  if(readFromSocket()) {
    act();
  }
  // delay(1000);
  // Serial.println("Hello");
}