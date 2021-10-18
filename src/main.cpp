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
struct Packet_t {
  uint8_t len;
  byte emptyForLaterUse[2];
  uint8_t op;
  byte data[];
};

Packet_t* createPacket() {
  Packet_t* packetTemp = (Packet_t*)malloc(sizeof(packetTemp->len));
  packetTemp->len=0;
  return packetTemp;
}

Packet_t* packet = createPacket();

const uint8_t OP_RESET = 0b00000000;
const uint8_t OP_SET_ST_SSID = 0b00000001;
const uint8_t OP_SET_ST_PASS = 0b00000010;
const uint8_t OP_CONNECT_TO_ST = 0b00000011;
const uint8_t OP_DISABLE_AP = 0b00000100;
const uint8_t OP_STATUS = 0b00000101;

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

bool connectST(String stSsid, String stPass) {
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(stSsid.c_str(), stPass.c_str());

  int waitForConnect = 0;
  while(WiFi.status() != WL_CONNECTED){
    if(waitForConnect >= 600) {
      return false;
    }
    waitForConnect++;
    delay(100);
  }
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
    uint8_t lenArr[1];
    remoteClient.read(lenArr, 1);
    packet->len = lenArr[0];
    Serial.print("len: ");
    Serial.println(packet->len);

    packet = (Packet_t*)realloc(packet, packet->len);

    remoteClient.read(packet->emptyForLaterUse, 2);
    
    uint8_t opArr[1];
    remoteClient.read(opArr, 1);
    packet->op = opArr[0];
    Serial.print("op: ");
    Serial.println(packet->op);

    if (packet->len > sizeof(packet) && remoteClient.connected() && remoteClient.available()) {
      Serial.println("in packet data");
      remoteClient.read(packet->data, packet->len-4);
    }

    return true;
  }

  return false;
}

void resErr(uint8_t op, uint8_t errCode) {
  uint8_t res[2] = {op, errCode};
  remoteClient.write(res, 2);
}

void resOK(uint8_t op) {
  uint8_t res[2] = {op, 0b00000000};
  remoteClient.write(res, 2);
}

void resOKData(uint8_t op, String data) {
  char dataArr[data.length()+3];
  dataArr[0] = op;
  dataArr[1] = 0b11111111;
  data.toCharArray(&dataArr[2], data.length()+1);
  printArray((byte*)dataArr, sizeof(dataArr));
  remoteClient.write((uint8_t*)dataArr, data.length()+3);
}

void act() {
  if(packet->op == OP_SET_ST_PASS) {
    preferences.putString(ST_SSID_KEY, (char*)packet->data);
    resOK(packet->op);

  } else if (packet->op == OP_SET_ST_PASS) {
    preferences.putString(ST_PASS_KEY, (char*)packet->data);
    resOK(packet->op);

  } else if (packet->op == OP_CONNECT_TO_ST) {
    connectST(preferences.getString(ST_SSID_KEY), preferences.getString(ST_PASS_KEY));
    String ip = WiFi.localIP().toString();
    resOKData(packet->op, ip);

  } else if (packet->op == OP_RESET) {
    resOK(packet->op);
    ESP.restart();

  } else if (packet->op == OP_DISABLE_AP) {
    resOK(packet->op);
    // TODO make sure it does not disconnect the wifi from station.
    WiFi.mode(WIFI_STA);

  } else if (packet->op == OP_STATUS) {
    String status = "{\"ok\":1,\"wifi\":";
    status.concat((int)WiFi.status());
    status += ",\"savedSSID\":\"" + preferences.getString(ST_SSID_KEY);
    status += "\",\"gatewayIP\":\"" + WiFi.gatewayIP();
    status += "\",\"localIP\":\"" + WiFi.localIP();
    status += "\"}";
    Serial.println(status);
    resOKData(packet->op, status);
  } else {
    resErr(packet->op, 0b00000001);
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
    if(!connectST(stSsid, stPass)) {
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