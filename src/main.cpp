#include <Arduino.h>
#include <ArduinoJson.h>
#include <namedMesh.h>

#define MESH_SSID "smartdoornetwork"
#define MESH_PASSWORD "t4np454nd1"
#define LED 2
#define MESH_PORT 5555
String GATEWAY_ID = "nkXgI";
String GATEWAY_FULL_NAME = "GATEWAY-" + GATEWAY_ID;
String NODE_DESTINATION = "NODE-EkXeg";
String serial_data_in;
boolean isResponseDestinationCorrect = false;
unsigned long messageTimestamp = 0;

Scheduler userScheduler;
namedMesh mesh;

void setup() {
  Serial.begin(115200);

  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

  mesh.setDebugMsgTypes(ERROR | STARTUP);
  mesh.init(MESH_SSID, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.setName(GATEWAY_FULL_NAME);

  mesh.onReceive([](String &from, String &msg) {
    // Serial.printf("[i]: Receiving Request From Node: %s. %s\n", from.c_str(),
    //               msg.c_str());
    // Pastikan Data Yang Diterima Memang Ditujukan Untuk Node Ini, Ubah Data
    // menjadi JSON Terlebih dahulu
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, msg.c_str());
    if (error) {
      Serial.print("Failed to deserializeJson: ");
      Serial.println(error.f_str());
      return;
    }

    if (doc["destination"] == GATEWAY_FULL_NAME) {
      isResponseDestinationCorrect = true;
    }

    // Jika tipe response yang diterima adalah "auth"
    if (isResponseDestinationCorrect && doc["type"] == "auth") {
      Serial.printf("[i]: Receiving Auth Request From Node: %s. %s\n",
                    from.c_str(), msg.c_str());
    }

    // Jika response yang diterima adalah "connectionstartup"
    if (isResponseDestinationCorrect && doc["type"] == "connectionstartup") {
      Serial.println("Sending Response For Connection Start Up");
      String destination = doc["source"];
      String msg = "{\"type\":\"connectionstartup\", \"source\":\"" +
                   GATEWAY_FULL_NAME + "\", \"destination\" : \"" +
                   destination + "\", \"success\":true}";
      mesh.sendSingle(destination, msg);
    }
  });

  mesh.onChangedConnections(
      []() { Serial.printf(" [M]: Changed Connection\n"); });
}

void loop() {
  mesh.update();
  if (Serial.available() > 0) {
    serial_data_in = Serial.readStringUntil('\n');
    // Change String To JSON
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, serial_data_in);
    // Test if parsing succeeds.
    if (error) {
      Serial.println("Failed to deserializeJson");
      return;
    }
    if (doc["type"] == "auth") {
      digitalWrite(LED, HIGH);
      String NEW_NODE_DESTINATION = doc["destination"];
      mesh.sendSingle(NEW_NODE_DESTINATION, serial_data_in);
      digitalWrite(LED, LOW);
    }
  }
}