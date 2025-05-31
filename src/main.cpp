#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <DHT.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <ElegantOTA.h>

// Constants
#define DHTPIN 21
#define DHTTYPE DHT22
#define DHT_TIMEOUT 2000
#define MAX_DATA_POINTS 504

// Reduced JSON capacity to save memory
const size_t JSON_CAPACITY = 50000;
const unsigned long MAX_REASONABLE_TIMESTAMP = 1800000000UL;

// Global objects
DHT dht(DHTPIN, DHTTYPE);
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0);

bool waitForTimeSync(unsigned long timeoutMs = 5000) {
  unsigned long start = millis();
  while (!timeClient.update()) {
    if (millis() - start > timeoutMs) {
      Serial.println("Failed to sync time with NTP.");
      return false;
    }
    delay(200);
  }
  Serial.printf("Time synced: %lu\n", timeClient.getEpochTime());
  return true;
}

Preferences preferences;
bool skipNextLoopLog = false;
float alertThreshold = 95.0;
float humidityThreshold = 40.0;

// Variables for sensor readings and timer
float temperature = 0.0;
float humidity = 0.0;
unsigned long incubationStartTime = 0;

// Data storage for graphs
struct DataPoint {
  unsigned long timestamp;
  float temperature;
  float humidity;
};
DataPoint dataHistory[MAX_DATA_POINTS];
int dataCount = 0;
unsigned long lastDataLogTime = 0;

// Function declarations
String getTemperature();
String getHumidity();
String getIncubationTime();
void resetIncubationTimer();
String getDataJSON();
void logDataPoint();
void loadDataFromFile();
void saveDataToFile();
void addDataPoint(unsigned long timestamp, float temp, float humid);
void sendWebSocketUpdate();

// WebSocket Event Handler
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.println("WebSocket client connected");
    float newTemp = dht.readTemperature(true);
    float newHumid = dht.readHumidity();
    if (!isnan(newTemp) && newTemp != 0.0) {
      temperature = newTemp;
      Serial.print("Immediate Temperature: ");
      Serial.println(temperature);
    } else {
      Serial.println("Failed immediate temperature read");
    }
    if (!isnan(newHumid) && newHumid != 0.0) {
      humidity = newHumid;
      Serial.print("Immediate Humidity: ");
      Serial.println(humidity);
    } else {
      Serial.println("Failed immediate humidity read");
    }
    sendWebSocketUpdate();
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.println("WebSocket client disconnected");
  }
}

// SPIFFS Data Functions
void loadDataFromFile() {
  if (!SPIFFS.exists("/data.json")) {
    dataCount = 0;
    return;
  }
  File file = SPIFFS.open("/data.json", FILE_READ);
  if (!file) {
    Serial.println("Failed to open data file for reading");
    dataCount = 0;
    return;
  }
  size_t size = file.size();
  if (size == 0) {
    dataCount = 0;
    file.close();
    return;
  }
  std::unique_ptr<char[]> buf(new char[size]);
  file.readBytes(buf.get(), size);
  file.close();

  DynamicJsonDocument doc(JSON_CAPACITY);
  DeserializationError error = deserializeJson(doc, buf.get());
  if (error) {
    Serial.print("Failed to parse data file: ");
    Serial.println(error.c_str());
    dataCount = 0;
    return;
  }
  JsonArray array = doc.as<JsonArray>();
  dataCount = array.size();
  int i = 0;
  for (JsonObject point : array) {
    dataHistory[i].timestamp = point["timestamp"];
    dataHistory[i].temperature = roundf(point["temperature"].as<float>() * 10.0f) / 10.0f;
    dataHistory[i].humidity = roundf(point["humidity"].as<float>() * 10.0f) / 10.0f;
    i++;
    if (i >= MAX_DATA_POINTS) break;
  }
  Serial.printf("Loaded %d data points from SPIFFS\n", dataCount);
}

void saveDataToFile() {
  DynamicJsonDocument doc(JSON_CAPACITY);
  JsonArray array = doc.to<JsonArray>();
  for (int i = 0; i < dataCount; i++){
    if ((i & 0x1F) == 0) yield();   
    JsonObject point = array.createNestedObject();
    point["timestamp"] = dataHistory[i].timestamp;
    float roundedTemp = roundf(dataHistory[i].temperature * 10.0) / 10.0;
    float roundedHumid = roundf(dataHistory[i].humidity * 10.0) / 10.0;
    point["temperature"] = roundedTemp;
    point["humidity"] = roundedHumid;
  }
  File file = SPIFFS.open("/data.json", FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open data file for writing");
    return;
  }
  serializeJson(doc, file);
  file.close();
  Serial.printf("Saved %d data points to SPIFFS\n", dataCount);
}

// Helper Functions
String getTemperature() {
  if (isnan(temperature)) return "Error";
  return String(temperature, 1);
}

String getHumidity() {
  if (isnan(humidity)) return "Error";
  return String(humidity, 1);
}

String getIncubationTime() {
  if (incubationStartTime == 0 || timeClient.getEpochTime() < 1600000000)
    return "Waiting for time sync...";
  unsigned long elapsedSeconds = timeClient.getEpochTime() - incubationStartTime;
  int days = elapsedSeconds / 86400;
  int hours = (elapsedSeconds / 3600) % 24;
  int minutes = (elapsedSeconds / 60) % 60;
  char buffer[30];
  if (days > 0)
    sprintf(buffer, "%dD %02dH %02dM", days, hours, minutes);
  else if (hours > 0)
    sprintf(buffer, "%dH %02dM", hours, minutes);
  else
    sprintf(buffer, "%dM", minutes);
  return String(buffer);
}

String getDataJSON() {
  DynamicJsonDocument doc(JSON_CAPACITY);
  JsonArray array = doc.to<JsonArray>();
  for (int i = 0; i < dataCount; i++){
    JsonObject point = array.createNestedObject();
    point["timestamp"] = dataHistory[i].timestamp;
    float roundedTemp = roundf(dataHistory[i].temperature * 10.0) / 10.0;
    float roundedHumid = roundf(dataHistory[i].humidity * 10.0) / 10.0;
    point["temperature"] = roundedTemp;
    point["humidity"] = roundedHumid;
  }
  String result;
  serializeJson(doc, result);
  return result;
}

void addDataPoint(unsigned long timestamp, float temp, float humid) {
  if (dataCount >= MAX_DATA_POINTS) {
    for (int i = 0; i < MAX_DATA_POINTS - 1; i++) {
      dataHistory[i] = dataHistory[i + 1];
    }
    dataCount = MAX_DATA_POINTS - 1;
  }

  float roundedTemp = roundf(temp * 10.0f) / 10.0f;
  float roundedHumid = roundf(humid * 10.0f) / 10.0f;

  dataHistory[dataCount].timestamp = timestamp;
  dataHistory[dataCount].temperature = roundedTemp;
  dataHistory[dataCount].humidity = roundedHumid;
  dataCount++;
}

void resetIncubationTimer() {
  incubationStartTime = timeClient.getEpochTime();
  lastDataLogTime = 0;
  dataCount = 0;
  SPIFFS.remove("/data.json");

  preferences.begin("egg-timer", false);
  preferences.putULong("startTime", incubationStartTime);
  preferences.end();
  Serial.println("Incubation timer reset and SPIFFS data cleared");
}

void logDataPoint() {
  unsigned long sensorTime = timeClient.getEpochTime();
  if (sensorTime > MAX_REASONABLE_TIMESTAMP) {
    Serial.println("Detected erroneous future timestamp; skipping data point");
    return;
  }

  if (!isnan(temperature) && !isnan(humidity) && temperature != 0.0 && humidity != 0.0) {
    float roundedTemp = roundf(temperature * 10.0f) / 10.0f;
    float roundedHumid = roundf(humidity * 10.0f) / 10.0f;
    addDataPoint(sensorTime, roundedTemp, roundedHumid);
    saveDataToFile();
    Serial.println("Data point logged");
  } else {
    Serial.println("Invalid sensor readings; skipping data point");
  }
}

// Simplified WebSocket update - removed chunking to save memory
void sendWebSocketUpdate() {
  float sumTemp = 0, sumHumid = 0;
  int count = 0;
  float minTemp = 1000, maxTemp = -1000, minHumid = 1000, maxHumid = -1000;
  unsigned long now = timeClient.getEpochTime();
  
  for (int i = 0; i < dataCount; i++){
    if (dataHistory[i].timestamp >= now - 86400) {
      float t = dataHistory[i].temperature;
      float h = dataHistory[i].humidity;
      sumTemp += t;
      sumHumid += h;
      if (t < minTemp) minTemp = t;
      if (t > maxTemp) maxTemp = t;
      if (h < minHumid) minHumid = h;
      if (h > maxHumid) maxHumid = h;
      count++;
    }
  }
  
  float sumTempAll = 0, sumHumidAll = 0;
  int countAll = 0;
  float minTempAll = 1000, maxTempAll = -1000, minHumidAll = 1000, maxHumidAll = -1000;
  for (int i = 0; i < dataCount; i++){
    float t = dataHistory[i].temperature;
    float h = dataHistory[i].humidity;
    sumTempAll += t;
    sumHumidAll += h;
    if (t < minTempAll) minTempAll = t;
    if (t > maxTempAll) maxTempAll = t;
    if (h < minHumidAll) minHumidAll = h;
    if (h > maxHumidAll) maxHumidAll = h;
    countAll++;
  }
  
  String json = "{";
  json += "\"type\":\"update\",";
  json += "\"temperature\":" + String(temperature, 1) + ",";
  json += "\"humidity\":" + String(humidity, 1) + ",";
  json += "\"incubationTime\":\"" + getIncubationTime() + "\",";
  json += "\"startTime\":" + String(incubationStartTime) + ",";
  if (count > 0) {
    float avgTemp = sumTemp / count;
    float avgHumid = sumHumid / count;
    json += "\"summary\":{";
    json += "\"avgTemp\":" + String(avgTemp, 1) + ",";
    json += "\"minTemp\":" + String(minTemp, 1) + ",";
    json += "\"maxTemp\":" + String(maxTemp, 1) + ",";
    json += "\"avgHumid\":" + String(avgHumid, 1) + ",";
    json += "\"minHumid\":" + String(minHumid, 1) + ",";
    json += "\"maxHumid\":" + String(maxHumid, 1);
    json += "},";
  } else {
    json += "\"summary\":null,";
  }
  
  if (countAll > 0) {
    float avgTempAll = sumTempAll / countAll;
    float avgHumidAll = sumHumidAll / countAll;
    json += "\"allSummary\":{";
    json += "\"avgTemp\":" + String(avgTempAll, 1) + ",";
    json += "\"minTemp\":" + String(minTempAll, 1) + ",";
    json += "\"maxTemp\":" + String(maxTempAll, 1) + ",";
    json += "\"avgHumid\":" + String(avgHumidAll, 1) + ",";
    json += "\"minHumid\":" + String(minHumidAll, 1) + ",";
    json += "\"maxHumid\":" + String(maxHumidAll, 1);
    json += "}";
  } else {
    json += "\"allSummary\":null";
  }
  
  json += "}";
  
  // Simple send without chunking to save memory
  ws.textAll(json);
}

// Set Start Time Handler
void handleSetStartTime(AsyncWebServerRequest *request) {
  String daysParam = (request->hasParam("days") ? request->getParam("days")->value() : "0");
  String hoursParam = (request->hasParam("hours") ? request->getParam("hours")->value() : "0");
  int days = daysParam.toInt();
  int hours = hoursParam.toInt();
  unsigned long offset = days * 86400UL + hours * 3600UL;

  incubationStartTime = timeClient.getEpochTime() - offset;

  preferences.begin("egg-timer", false);
  preferences.putULong("startTime", incubationStartTime);
  preferences.end();

  Serial.printf("Updated startTime to %lu (offset %lu seconds)\n", incubationStartTime, offset);

  dataCount = 0;
  SPIFFS.remove("/data.json");

  float newTemp = dht.readTemperature(true);
  float newHumid = dht.readHumidity();
  unsigned long now = timeClient.getEpochTime();

  if (!isnan(newTemp) && newTemp != 0.0 && !isnan(newHumid) && newHumid != 0.0) {
    temperature = newTemp;
    humidity = newHumid;
    skipNextLoopLog = true;
    lastDataLogTime = now;
    logDataPoint();
    Serial.println("Initial data point logged after /setstarttime");
    sendWebSocketUpdate();
  } else {
    Serial.println("Skipping initial data log after /setstarttime due to invalid sensor reading");
  }

  request->send(200, "text/plain", "Egg start time updated and history cleared.");
}

// Setup Function
void setup() {
  Serial.begin(115200);
  Serial.println("Starting setup...");

  dht.begin();
  delay(2000);
  Serial.println("DHT sensor initialized");

  if (!SPIFFS.begin(true)) {
    Serial.println("An error occurred while mounting SPIFFS");
    return;
  }
  Serial.println("SPIFFS mounted");

  // SPIFFS Debug: list files
  Serial.println("Listing SPIFFS contents:");
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file) {
    Serial.printf("  %s (size: %u bytes)\n", file.name(), file.size());
    file = root.openNextFile();
  }
  Serial.println("End of SPIFFS listing");

  loadDataFromFile();
  
  Serial.printf("Free heap before WiFi: %d bytes\n", ESP.getFreeHeap());
  
  preferences.begin("egg-timer", false);
  unsigned long storedStart = preferences.getULong("startTime", 0);
  if (storedStart == 0) {
    incubationStartTime = timeClient.getEpochTime();
    preferences.putULong("startTime", incubationStartTime);
    Serial.println("No stored start time. Initialized new incubation timer.");
  } else {
    incubationStartTime = storedStart;
    Serial.println("Loaded stored incubation start time.");
  }

  preferences.begin("threshold-store", false);
  if (!preferences.isKey("threshold")) {
    preferences.putFloat("threshold", 95.0);
    alertThreshold = 95.0;
    Serial.println("Threshold not found. Setting default to 95.0");
  } else {
    alertThreshold = preferences.getFloat("threshold", 95.0);
    Serial.print("Loaded saved threshold: ");
    Serial.println(alertThreshold);
  }
  preferences.end();

  preferences.begin("threshold-store", false);
  if (!preferences.isKey("humidity")) {
    preferences.putFloat("humidity", 40.0);
    humidityThreshold = 40.0;
    Serial.println("Humidity threshold not found. Setting default to 40.0");
  } else {
    humidityThreshold = preferences.getFloat("humidity", 40.0);
    Serial.print("Loaded saved humidity threshold: ");
    Serial.println(humidityThreshold);
  }
  preferences.end();

  WiFiManager wifiManager;
  wifiManager.setAPStaticIPConfig(IPAddress(192,168,4,1),
                                  IPAddress(192,168,4,1),
                                  IPAddress(255,255,255,0));
  Serial.println("Attempting WiFi connection...");
  wifiManager.autoConnect("Incubuddy-Setup");
  Serial.println("WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  timeClient.begin();
  timeClient.setTimeOffset(0);
  waitForTimeSync();
  Serial.println("NTP client started");

  if (MDNS.begin("IncuBuddy")) {
    Serial.println("MDNS responder started");
  } else {
    Serial.println("Error setting up MDNS responder!");
  }

  ElegantOTA.begin(&server);
  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);

  ElegantOTA.onStart([]() {
    Serial.println("OTA update started");
  });
  ElegantOTA.onEnd([](bool success) {
    Serial.println("OTA update finished. Rebooting...");
    if (success) {
      Serial.println("Update successful");
    } else {
      Serial.println("Update failed");
    }
    delay(1000);
  });
  Serial.println("OTA Update Web Interface started at /update");

  // Restart endpoint
  server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "Restarting...");
    delay(100);
    ESP.restart();
  });

  // Serve /data.json as download
  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/data.json", "application/json", true);
  });

  // Upload JSON HTML page - now served from SPIFFS
  server.on("/upload_json", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/upload.html", "text/html");
  });  

  server.on("/upload_json", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Upload complete. Reboot device or refresh chart.");
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    static File uploadFile;

    if (index == 0) {
      if (SPIFFS.exists("/data.json")) {
        SPIFFS.remove("/data.json");
      }
      uploadFile = SPIFFS.open("/data.json", FILE_WRITE);
    }

    if (uploadFile) {
      uploadFile.write(data, len);
    }

    if (final) {
      uploadFile.close();
    }
  });

  // HTTP routes - serve HTML from SPIFFS instead of program memory
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("Root page requested");
    request->send(SPIFFS, "/index.html", "text/html");
  });

  server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("Temperature requested");
    request->send(200, "text/plain", getTemperature());
  });

  server.on("/humidity", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("Humidity requested");
    request->send(200, "text/plain", getHumidity());
  });

  server.on("/time", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("Time requested");
    request->send(200, "text/plain", getIncubationTime());
  });

  server.on("/starttime", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (incubationStartTime == 0)
      request->send(200, "text/plain", "Not started");
    else
      request->send(200, "text/plain", String(incubationStartTime));
  });

  server.on("/setstarttime", HTTP_GET, handleSetStartTime);

  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("Timer reset requested");
    resetIncubationTimer();

    float newTemp = dht.readTemperature(true);
    float newHumid = dht.readHumidity();
    unsigned long now = timeClient.getEpochTime();

    if (!isnan(newTemp) && newTemp != 0.0 && !isnan(newHumid) && newHumid != 0.0) {
      temperature = newTemp;
      humidity = newHumid;
      skipNextLoopLog = true;
      lastDataLogTime = now;
      logDataPoint();
      Serial.println("Initial data point logged after reset");
      sendWebSocketUpdate();
    } else {
      Serial.println("Skipping initial data log after reset due to invalid sensor reading");
    }

    request->send(200, "text/plain", "Timer and all data reset");
  });

  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("Chart data requested");
    request->send(200, "application/json", getDataJSON());
  });

  // Threshold endpoints
  server.on("/getthreshold", HTTP_GET, [](AsyncWebServerRequest *request){
    preferences.begin("threshold-store", true);
    float threshold = preferences.getFloat("threshold", 95.0);
    preferences.end();
    request->send(200, "text/plain", String(threshold, 1));
  });

  server.on("/setthreshold", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("value")) {
      float threshold = request->getParam("value")->value().toFloat();
      preferences.end();
      if (preferences.begin("threshold-store", false)) {
        preferences.putFloat("threshold", threshold);
        preferences.end();
        alertThreshold = threshold;
        sendWebSocketUpdate();
        request->send(200, "text/plain", "Threshold saved: " + String(threshold, 1));
      } else {
        request->send(500, "text/plain", "Failed to open preferences namespace");
      }
    } else {
      request->send(400, "text/plain", "Missing value param");
    }
  });

  server.on("/gethumidity", HTTP_GET, [](AsyncWebServerRequest *request){
    preferences.begin("threshold-store", true);
    float threshold = preferences.getFloat("humidity", 40.0);
    preferences.end();
    request->send(200, "text/plain", String(threshold, 1));
  });

  server.on("/sethumidity", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("value")) {
      float threshold = request->getParam("value")->value().toFloat();
      preferences.begin("threshold-store", false);
      preferences.putFloat("humidity", threshold);
      preferences.end();
      humidityThreshold = threshold;
      request->send(200, "text/plain", "Humidity threshold saved: " + String(threshold, 1));
      sendWebSocketUpdate();
    } else {
      request->send(400, "text/plain", "Missing value param");
    }
  });

  // Serve favicon
  server.serveStatic("/favicon.ico", SPIFFS, "/favicon.ico").setCacheControl("max-age=86400");

  Serial.printf("Free heap after server setup: %d bytes\n", ESP.getFreeHeap());
  server.begin();
  Serial.println("Web server started!");
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    timeClient.update();
    unsigned long currentEpoch = timeClient.getEpochTime();

    // Log data point every hour (3600 seconds)
    if (currentEpoch > 1600000000 &&
        (lastDataLogTime == 0 || currentEpoch - lastDataLogTime >= 3600)) {

      if (skipNextLoopLog) {
        Serial.println("Skipping one loop-triggered data log (already logged manually)");
        skipNextLoopLog = false;
      } else {
        logDataPoint();
        lastDataLogTime = currentEpoch;
        Serial.println("Data point logged from loop");
      }
    }
  

    // Update sensor readings every minute
    static unsigned long lastSensorUpdate = 0;
    if (millis() - lastSensorUpdate > 60000) {
      float newTemp = dht.readTemperature(true);
      float newHumid = dht.readHumidity();
      if (!isnan(newTemp) && newTemp != 0.0) {
        temperature = newTemp;
        Serial.print("Temperature: ");
        Serial.print(temperature);
        Serial.println(" °F");
      } else {
        Serial.println("Failed to read temperature!");
      }
      if (!isnan(newHumid) && newHumid != 0.0) {
        humidity = newHumid;
        Serial.print("Humidity: ");
        Serial.print(humidity);
        Serial.println(" %");
      } else {
        Serial.println("Failed to read humidity!");
      }
      lastSensorUpdate = millis();
      sendWebSocketUpdate();
    }
  }
  
  static unsigned long lastWifiCheck = 0;
  if (millis() - lastWifiCheck > 10000) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi connection lost. Reconnecting...");
      WiFi.reconnect();
    }
    lastWifiCheck = millis();
  }
  ElegantOTA.loop();
  delay(10);
}