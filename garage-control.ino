#include <WiFi.h>
#include <NetworkClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "my_wifi.h"

WebServer server(80);

const int GARAGE_UP = 2;
const int GARAGE_DOWN = 22;
const int GARAGE_STOP = 23;
const int GARAGE_COMMON = 20;

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
  forceStop();

  // Garage Operation
  digitalWrite(control, LOW);
  digitalWrite(GARAGE_COMMON, LOW);

  delay(200);
  digitalWrite(GARAGE_COMMON, HIGH);
  digitalWrite(control, HIGH);
  delay(200);
  
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

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");
}

void loop(void) {
  server.handleClient();
  delay(2);  //allow the cpu to switch to other tasks
}
