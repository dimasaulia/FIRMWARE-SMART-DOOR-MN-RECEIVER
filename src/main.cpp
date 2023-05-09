#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <namedMesh.h>

#define MESH_SSID "smartdoornetwork"
#define MESH_PASSWORD "t4np454nd1"
#define LED 2
#define MESH_PORT 5555

unsigned long messageTimestamp = 0;
unsigned long requestCredentialTimestamp = 0;
const int requestCredentialTimeInterval = 5000;

// File paths to save input values permanentlys
const char *ssidPath = "/ssid.txt";
const char *passwordPath = "/password.txt";
const char *gatewayPath = "/gateway.txt";

String GATEWAY_FULL_NAME;
String DATA_PASSWORD;
String DATA_SSID;

String serial_data_in;
boolean isResponseDestinationCorrect = false;
boolean isConnectionReady = false;
boolean isNetworkReady = false;
boolean waitingForNetworkCredential;

// Class Instance
Scheduler userScheduler;
namedMesh mesh;

// Initialize SPIFFS
void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("[e]: An error has occurred while mounting SPIFFS");
  }
  Serial.println("[x]: SPIFFS mounted successfully");
}

// Read File from SPIFFS
String readFile(fs::FS &fs, const char *path) {
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.println("- failed to open file for reading");
    return String();
  }

  String fileContent;
  while (file.available()) {
    fileContent = file.readStringUntil('\n');
    break;
  }
  return fileContent;
}

// Write file to SPIFFS
void writeFile(fs::FS &fs, const char *path, const char *message) {
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("- file written");
  } else {
    Serial.println("- write failed");
  }
}

// Initialize Mesh
bool meshStatus() {
  if (DATA_SSID == "" || DATA_PASSWORD == "" || GATEWAY_FULL_NAME == "") {
    Serial.println("[e]: Undefined SSID, Password, Gateway");
    return false;
  }
  return true;
}

// Reset Mesh Network
void meshReset() {
  Serial.println("Attempting To Reset ESP Mesh Network");
  String empty = "";
  writeFile(SPIFFS, ssidPath, empty.c_str());
  writeFile(SPIFFS, passwordPath, empty.c_str());
  writeFile(SPIFFS, gatewayPath, empty.c_str());
  delay(3000);
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  initSPIFFS();

  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

  Serial.println("Try To Reading SPIFFS");
  DATA_SSID = readFile(SPIFFS, ssidPath);
  DATA_PASSWORD = readFile(SPIFFS, passwordPath);
  GATEWAY_FULL_NAME = readFile(SPIFFS, gatewayPath);
  Serial.print("SSID: ");
  Serial.println(DATA_SSID);
  Serial.print("Password: ");
  Serial.println(DATA_PASSWORD);
  Serial.print("Gateway: ");
  Serial.println(GATEWAY_FULL_NAME);

  // Cek Network Credential Pada SPIFFS
  if (meshStatus() == false) {
    // Jika Belum Maka Request Ke Gateway
    waitingForNetworkCredential = true;
    Serial.println("__REQUEST_NETWORK_CREDENTIAL");
  }

  // Tunggu Respon Diberikan Oleh Gateway
  Serial.println("[x] WAITING FOR GATEWAY RESPONSE");
  while (waitingForNetworkCredential) {
    // Keep Sending Request Network Credential Every 5000ms
    if (millis() > requestCredentialTimestamp + requestCredentialTimeInterval) {
      requestCredentialTimestamp = millis();
      Serial.println("__REQUEST_NETWORK_CREDENTIAL");
    }

    // Jika Gateway Sudah Memberikan Respone
    if (Serial.available() > 0) {
      serial_data_in = Serial.readStringUntil('\n');
      // Change String To JSON
      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, serial_data_in);

      if (doc["type"] == "networksetup") {
        waitingForNetworkCredential = false;
        isNetworkReady = true;
        String serialSSID = doc["SSID"];
        String serialPASSWORD = doc["PASSWORD"];
        String serialGATEWAY = doc["GATEWAY"];
        writeFile(SPIFFS, ssidPath, serialSSID.c_str());
        writeFile(SPIFFS, passwordPath, serialPASSWORD.c_str());
        writeFile(SPIFFS, gatewayPath, serialGATEWAY.c_str());
        Serial.println("Get Credential, ESP Restart");
        delay(3000);
        ESP.restart();
      }
    }
  }

  // Ubah Status Menjadi Network Ready
  if (meshStatus() == true) {
    isNetworkReady = true;
  }

  if (isNetworkReady) {
    mesh.setDebugMsgTypes(ERROR | STARTUP);
    mesh.init(DATA_SSID, DATA_PASSWORD, &userScheduler, MESH_PORT);
    mesh.setName(GATEWAY_FULL_NAME);
    isConnectionReady = true;

    mesh.onReceive([](String &from, String &msg) {
      // Serial.printf("[i]: Receiving Request From Node: %s. %s\n",
      // from.c_str(),
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

      String destination =
          doc["destination"]; // Destinasi akhir data yang diterima,dan akan
                              // menjadi sumber data (source) ketika response
                              // balik diberikan
      String source =
          doc["source"]; // Sumber pengirim data, dan akan menjadi destinasi
                         // akhir ketika response balik diberikan
      String type = doc["type"];

      if (destination == GATEWAY_FULL_NAME) {
        isResponseDestinationCorrect = true;
      }

      // Jika tipe response yang diterima adalah "auth"
      if (isResponseDestinationCorrect && type == "auth") {
        Serial.printf("__REQUEST_FOR_AUTH: %s. %s\n", from.c_str(),
                      msg.c_str());
      }

      // Jika response yang diterima adalah "connectionstartup"
      if (isResponseDestinationCorrect && type == "connectionstartup") {
        Serial.println("[x]: Sending Response For Connection Start Up");
        String payload = "{\"type\":\"connectionstartup\", \"source\":\"" +
                         GATEWAY_FULL_NAME + "\", \"destination\" : \"" +
                         source + "\", \"success\":true}";
        mesh.sendSingle(source, payload);
      }

      // Jika response yang diterima adalah "connectionping"
      if (isResponseDestinationCorrect && type == "connectionping") {
        Serial.println("Sending Response For Connection Ping");
        String payload = "{\"type\":\"connectionping\", \"source\":\"" +
                         GATEWAY_FULL_NAME + "\", \"destination\" : \"" +
                         source + "\", \"success\":true}";
        mesh.sendSingle(source, payload);
        Serial.printf("__CONNECTION_PING: %s. %s\n", from.c_str(), msg.c_str());
      }
    });

    mesh.onChangedConnections(
        []() { Serial.printf(" [M]: Changed Connection\n"); });
  }
}

void loop() {
  if (isConnectionReady) {
    mesh.update(); // Updateing Network

    // Waiting Gateway Send Data
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
}