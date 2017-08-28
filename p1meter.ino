#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h> 

#include "arduino-dsmr/src/dsmr.h"

const char* ssid = "NeFas";
const char* password = "";
const char* hostName = "p1meter";

const unsigned long publishInterval = 30000UL; // Publish every 5 seconds

const char* mqtt_server = "192.168.178.35";

// MQTT stuff
char ID[9] = {0};
WiFiClient espClient;
PubSubClient client(espClient);
const char* mqttDebugTopic = "nefas/debug/DSMR1";
const char* mqttTopic = "nefas/DSMR1"; // post /{sensorName} {sensorValue} {unit}

#define SERIAL_RX     4  // pin for SoftwareSerial RX
SoftwareSerial mySerial(SERIAL_RX, -1); // (RX, TX)

using MyData = ParsedData<
  /* String */ timestamp,
  
  /* FixedValue */ energy_delivered_tariff1,
  /* FixedValue */ energy_delivered_tariff2,
  /* FixedValue */ energy_returned_tariff1,
  /* FixedValue */ energy_returned_tariff2,
  /* String */ electricity_tariff,
  /* FixedValue */ power_delivered,
  /* FixedValue */ power_returned,

  /* uint32_t */ electricity_failures,
  /* uint32_t */ electricity_long_failures,

  /* TimestampedFixedValue */ gas_delivered
>;

struct Printer {
  template<typename Item>
  
  void apply(Item &i) {
    if (i.present()) {
      Serial.print(Item::name);
      Serial.print(F(": "));
      Serial.print(i.val());
      Serial.print(Item::unit());
      Serial.println();
    }
  }
};

// Read from software serial port, do not have a physical request pin (as we have it tied to 5V)
P1Reader reader(&mySerial, NOT_A_PIN);

unsigned long lastPublish = 0UL;
int value = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(hostName);

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  mySerial.begin(115200);

  uint32_t chipid = ESP.getChipId();
  snprintf(ID, sizeof(ID), "%x", chipid);

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  
  reader.enable(true);
  lastPublish = millis();
}


void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}


void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    
    // Attempt to connect
    if (client.connect(ID)) {
      Serial.println("connected");

      char msg[50] = {0};
      snprintf (msg, 75, "%s (re)connect #%ld", hostName, value);
      
      Serial.print("Publish message: ");
      Serial.println(msg);
      
      client.publish(mqttDebugTopic, msg);
      ++value;
      
      // client.subscribe(mqttTopic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void publishString(char* sensorName, String str, bool retained = false) {
  char topic[64] = {0};
  char message[64] = {0};

  sprintf(topic, "%s/%s", mqttTopic, sensorName);
  sprintf(message, "%s", str.c_str());
  
  client.publish(topic, message, retained);
}

void publishFixedK(char* sensorName, FixedValue value, char* unit, bool retained = false) {
  char topic[64] = {0};
  char message[64] = {0};

  sprintf(topic, "%s/%s", mqttTopic, sensorName);
  sprintf(message, "%d.%d %s", value.int_val()/1000, value.int_val() % 1000, unit);
  
  client.publish(topic, message, retained);
}

void publishFixed(char* sensorName, FixedValue value, char* unit, bool retained = false) {
  char topic[64] = {0};
  char message[64] = {0};

  sprintf(topic, "%s/%s", mqttTopic, sensorName);
  sprintf(message, "%d %s", value.int_val(), unit);
  
  client.publish(topic, message, retained);
}

void publishTSFixedK(char* sensorName, TimestampedFixedValue value, char* unit, bool retained = false) {
  char topic[64] = {0};
  char message[64] = {0};

  sprintf(topic, "%s/%s", mqttTopic, sensorName);
  sprintf(message, "%d.%d %s", value.int_val()/1000, value.int_val() % 1000, unit);
  
  client.publish(topic, message, retained);

  sprintf(topic, "%s/%s/timestamp", mqttTopic, sensorName);
  sprintf(message, "%s", value.timestamp.c_str());
  
  client.publish(topic, message, retained);
}

void publishInt(char* sensorName, uint32_t value, bool retained = false) {
  char topic[64] = {0};
  char message[64] = {0};

  sprintf(topic, "%s/%s", mqttTopic, sensorName);
  sprintf(message, "%d", value);
  
  client.publish(topic, message, retained);
}

void sendData(MyData data) {
  publishString("timestamp", data.timestamp, true);
  
  publishFixedK("energy_delivered_tariff1", data.energy_delivered_tariff1, "kWh", true);
  publishFixedK("energy_delivered_tariff2", data.energy_delivered_tariff2, "kWh", true);
  publishFixedK("energy_returned_tariff1", data.energy_returned_tariff1, "kWh", true);
  publishFixedK("energy_returned_tariff2", data.energy_returned_tariff2, "kWh", true);
  publishString("electricity_tariff", data.electricity_tariff, true);
  publishFixed("power_delivered", data.power_delivered, "W", true);
  publishFixed("power_returned", data.power_returned, "W", true);
  
  publishInt("electricity_failures", data.electricity_failures, true);
  publishInt("electricity_long_failures", data.electricity_long_failures, true);

  publishTSFixedK("gas_delivered", data.gas_delivered, "m3", true);
}

void loop() {
  reader.loop();

  yield();

  if (!client.connected()) {
    reconnect();
  }

  yield();

  if (millis() - lastPublish >= publishInterval) {
    // Enable P1 reader
    reader.enable(true);
    
    lastPublish = millis();
  }

  if (reader.available()) {
    // P1 data is available
    MyData data;
    String err;

    if (reader.parse(&data, &err)) {
      // Parse succesful, print result
      sendData(data);
      //client.subscribe(mqttTopic);
      Serial.println("==============================================");
      data.applyEach(Printer());
    } else {
      // Parser error, print error
      Serial.println(err);
      client.publish(mqttDebugTopic, (const char*) err.c_str(), false);
      //client.subscribe(mqttTopic); 
    }

    yield();

    reader.clear();
  }

  client.loop();
  
  ArduinoOTA.handle();
}



