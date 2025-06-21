#include <WiFi.h>
#include <Arduino.h>
#include <NetworkClient.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <esp_now.h>
#include "my_wifi.h"

#define RSSI_AT_1M -45   // RSSI estimation at the point of 1m. (adjustable)
#define PATH_LOSS_EXPONENT 2.0  // 2.0-3.5 in house

volatile int lastRSSI = -100;  // Init value when not recieving
volatile bool shouldSendLineMessage = false;
volatile bool rssiPreviouslyLow = true;
unsigned long lastSentTime = 0;
const unsigned long MIN_INTERVAL = 30UL * 60UL * 1000UL;

WebServer server(80);

const int GARAGE_UP = 2;
const int GARAGE_DOWN = 22;
const int GARAGE_STOP = 23;
const int GARAGE_COMMON = 20;

String generateUUIDv4() {
  uint8_t uuid[16];
  for (int i = 0; i < 16; i++) {
    uuid[i] = (uint8_t) random(0, 256);
  }

  // UUIDv4のバージョンビット（4）とvariantビット（8, 9, A, B）を設定
  uuid[6] = (uuid[6] & 0x0F) | 0x40; // version 4
  uuid[8] = (uuid[8] & 0x3F) | 0x80; // variant

  char uuidStr[37];
  sprintf(uuidStr,
    "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
    uuid[0], uuid[1], uuid[2], uuid[3],
    uuid[4], uuid[5],
    uuid[6], uuid[7],
    uuid[8], uuid[9],
    uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]
  );

  return String(uuidStr);
}

void sendLineMsg(char* msg) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure(); // skip check cert for experimental
  //client.setCACert(rootCACert);

  HTTPClient https;
  https.begin(client, "https://api.line.me/v2/bot/message/push");

  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", String("Bearer ") + CHANNEL_ACCESS_TOKEN);
  https.addHeader("X-Line-Retry-Key", generateUUIDv4());

  // JSONペイロードを構築
  String payload = "{";
  payload += "\"to\":\"" + String(USER_ID) + "\",";
  payload += "\"messages\":[{\"type\":\"text\",\"text\":\"" + String(msg) + "\"}]";
  payload += "}";

  int httpCode = https.POST(payload);

  if (httpCode > 0) {
    String response = https.getString();
    Serial.printf("LINE API Response (%d): %s\n", httpCode, response.c_str());
  } else {
    Serial.printf("POST failed: %s\n", https.errorToString(httpCode).c_str());
  }

  https.end();
}

// RSSI → distance estimation
float estimateDistance(int rssi) {
  float ratio = (float)(RSSI_AT_1M - rssi) / (10 * PATH_LOSS_EXPONENT);
  return pow(10, ratio);
}

// Call back for ESP-NOW
void onReceiveData(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  lastRSSI = WiFi.RSSI();
  unsigned long now = millis();

  if (lastRSSI > -75 && rssiPreviouslyLow && (now - lastSentTime >= MIN_INTERVAL)) {
    shouldSendLineMessage = true;
    rssiPreviouslyLow = false;
  }

  if (lastRSSI > -74) {
    rssiPreviouslyLow = true;
  }
  float distance = estimateDistance(lastRSSI);
  Serial.printf("RSSI: %d dBm | Estimate Distance : %.2f m\n", lastRSSI, distance);
}


void handleRoot() {
  digitalWrite(LED_BUILTIN, LOW);
  server.send(200, "application/json", "{\"status\":\"wake\"}");
  delay(1000);
  digitalWrite(LED_BUILTIN, HIGH);
}

void forceStop(){
  Serial.println("Force stop...");
  digitalWrite(GARAGE_STOP, LOW);
  digitalWrite(GARAGE_COMMON, LOW);

  delay(200);
  digitalWrite(GARAGE_COMMON, HIGH);
  digitalWrite(GARAGE_STOP, HIGH);
  delay(200);
}

void handleGarage(int control){
  // Force Stop before operation
  //forceStop();

  // Garage Operation
  digitalWrite(control, LOW);
  digitalWrite(GARAGE_COMMON, LOW);

  delay(300);
  digitalWrite(GARAGE_COMMON, HIGH);
  digitalWrite(control, HIGH);
  //delay(300);
  
  char *controlStr;
  switch(control){
    case GARAGE_UP:
      controlStr = "Garage Up";
      break;
    case GARAGE_DOWN:
      controlStr = "Garage Down";  
      break;
    case GARAGE_STOP:
      controlStr = "Garage Stop";   
      break;
    default:
      controlStr = "Unknown Status";   
  }

  String response = "{\"status\":\"" + String(controlStr) + "\"}";
  server.send(200, "application/json", response);
  Serial.println(controlStr);
}

void handleNotFound() {
  digitalWrite(GARAGE_UP, 1);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  digitalWrite(GARAGE_UP, 0);
}

void setup(void) {

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(GARAGE_UP, OUTPUT);
  pinMode(GARAGE_DOWN, OUTPUT);
  pinMode(GARAGE_STOP, OUTPUT);
  pinMode(GARAGE_COMMON, OUTPUT);

  digitalWrite(LED_BUILTIN, HIGH);
  digitalWrite(GARAGE_UP, HIGH);
  digitalWrite(GARAGE_DOWN, HIGH);
  digitalWrite(GARAGE_STOP, HIGH);
  digitalWrite(GARAGE_COMMON, HIGH);

  Serial.begin(115200);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("esp32")) {
    Serial.println("MDNS responder started");
  }

  // WiFi distance measurement initialization
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW 初期化失敗");
    return;
  }

  server.on("/", handleRoot);

  server.on("/garageup", []() {
    handleGarage(GARAGE_UP);
  });
  server.on("/garagedown", []() {
    handleGarage(GARAGE_DOWN);
  });
  server.on("/garagestop", []() {
    handleGarage(GARAGE_STOP);
  });
  server.on("/sendMsg", []() {
    sendLineMsg("元気？");
    String response = "{\"status\":\"sent\"}";
    server.send(200, "application/json", response);

  });

  server.onNotFound(handleNotFound);

  // Server Temporary stopped. Active receive callback instead.
  //server.begin();
  //Serial.println("HTTP server started");
  esp_now_register_recv_cb(onReceiveData);
}

void loop(void) {
  // Server Temporary stopped.
  //server.handleClient();
  if (shouldSendLineMessage) {
    sendLineMsg("RSSI -70 dBm reached, Garage Up!");
    handleGarage(GARAGE_UP);
    lastSentTime = millis();  
    shouldSendLineMessage = false;
  }
  delay(10);  //allow the cpu to switch to other tasks
}
