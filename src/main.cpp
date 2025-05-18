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
#include <ElegantOTA.h>  // Using the official ElegantOTA with async mode enabled

// Constants
#define DHTPIN 21           // DHT sensor pin
#define DHTTYPE DHT22       // DHT sensor type
#define DHT_TIMEOUT 2000    // Sensor read timeout (ms)
#define MAX_DATA_POINTS 504 // For 21 days at 1 hour per data point

// Increase JSON document capacity to store 21 days of data
const size_t JSON_CAPACITY = 50000;  // Adjust if needed

// Maximum acceptable timestamp (adjust based on your expected time range)
// For instance, if you're operating around 2025, you may not expect values above early 2027.
const unsigned long MAX_REASONABLE_TIMESTAMP = 1800000000UL;

// Global objects
DHT dht(DHTPIN, DHTTYPE);
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0);  // Use raw UTC
Preferences preferences; // Used to preserve the egg start time

// Variables for sensor readings and timer
float temperature = 0.0;
float humidity = 0.0;
unsigned long incubationStartTime = 0; // Preserved across reboots

// Data storage for graphs (stored in SPIFFS)
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

// --- WebSocket Event Handler ---
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                        AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.println("WebSocket client connected");
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.println("WebSocket client disconnected");
  }
}

// --- SPIFFS Data Functions ---
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
    dataHistory[i].temperature = point["temperature"];
    dataHistory[i].humidity = point["humidity"];
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
    // Round to one decimal place to save space
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

// --- Helper Functions ---
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
    // Round to one decimal place when sending JSON
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
    // Shift data points left (ring buffer behavior)
    for (int i = 0; i < MAX_DATA_POINTS - 1; i++) {
      dataHistory[i] = dataHistory[i + 1];
    }
    dataCount = MAX_DATA_POINTS - 1;
  }
  dataHistory[dataCount].timestamp = timestamp;
  dataHistory[dataCount].temperature = temp;
  dataHistory[dataCount].humidity = humid;
  dataCount++;
}

void resetIncubationTimer() {
  // Reset start time and clear historical data
  incubationStartTime = timeClient.getEpochTime();
  lastDataLogTime = 0;
  dataCount = 0;
  SPIFFS.remove("/data.json");
  preferences.putULong("startTime", incubationStartTime);
  Serial.println("Incubation timer reset and SPIFFS data cleared");
}

// --- Modified: Validate timestamp to prevent logging erroneous future values ---
void logDataPoint() {
  unsigned long sensorTime = timeClient.getEpochTime();
  // If the sensorTime exceeds our maximum acceptable value, skip logging.
  if (sensorTime > MAX_REASONABLE_TIMESTAMP) {
    Serial.println("Detected an erroneous future timestamp; skipping data point logging");
    return;
  }
  if (!isnan(temperature) && !isnan(humidity) && temperature != 0.0 && humidity != 0.0) {
    addDataPoint(sensorTime, temperature, humidity);
    saveDataToFile();
    Serial.println("Data point logged");
  } else {
    Serial.println("Invalid sensor readings; skipping data point logging");
  }
}

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
  
//  ws.textAll(json);
  // Instead of ws.textAll(json); do this:
  const size_t CHUNK_SIZE = 4096;            // adjust if needed
  size_t jsonLen = json.length();
  for (size_t offset = 0; offset < jsonLen; offset += CHUNK_SIZE) {
    size_t len = min(CHUNK_SIZE, jsonLen - offset);
    String part = json.substring(offset, offset + len);
    ws.textAll(part);
    yield();    // give the watchdog a chance
  }
}


// --- New Endpoint: Set Start Time ---
void handleSetStartTime(AsyncWebServerRequest *request) {
  String daysParam = (request->hasParam("days") ? request->getParam("days")->value() : "0");
  String hoursParam = (request->hasParam("hours") ? request->getParam("hours")->value() : "0");
  int days = daysParam.toInt();
  int hours = hoursParam.toInt();
  unsigned long offset = days * 86400UL + hours * 3600UL;
  incubationStartTime = timeClient.getEpochTime() - offset;
  preferences.putULong("startTime", incubationStartTime);
  // Clear historical data as well.
  dataCount = 0;
  SPIFFS.remove("/data.json");
  request->send(200, "text/plain", "Egg start time updated and history cleared.");
}

// --- Full HTML Page with all Buttons and Data Elements ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>Egg Incubator Monitor by Papa Lanc</title>
    <link rel="icon" type="image/x-icon" href="/favicon.ico">

    <!-- Bootstrap CSS -->
    <link rel="stylesheet" href="https://stackpath.bootstrapcdn.com/bootstrap/4.5.2/css/bootstrap.min.css" />

    <style>
      body.dark-mode {
        background-color: #343a40;
        color: #f8f9fa;
      }
      .dark-mode .card {
        background-color: #495057;
        color: #f8f9fa;
      }
      .dark-mode .navbar {
        background-color: #495057;
      }
      .main-reading {
        font-size: 2.5rem;
        font-weight: bold;
      }
      .time-range-btns .btn {
        margin-right: 5px;
      }
      .time-range-btns .btn.active {
        background-color: #007bff !important;
        border-color: #007bff !important;
        color: white !important;
      }
      .navbar-brand {
        font-size: 2rem;
        font-weight: bold;
      }
      .navbar {
        justify-content: center;
      }

      /* Add this */
      .page-header {
        text-align: center;
        margin-top: 30px;
        margin-bottom: 20px;
      }
      .page-header .title {
        font-size: 2.5rem;
        font-weight: bold;
      }
      .page-header .subtitle {
        font-size: 1.5rem;
        font-weight: bold;
        margin-top: 5px;
      }
    </style>
  </head>

  <body>
    <!-- START HEADER -->
    <div class="page-header">
      <div class="title">IncuBuddy</div>
      <div class="subtitle">Egg Incubator Monitor by Papa Lanc</div>
    </div>
    <!-- END HEADER -->

    <!-- Your existing body content goes here -->



<nav class="navbar navbar-expand navbar-light bg-light">
  <div class="ml-auto">
    <button class="btn btn-outline-secondary btn-sm" id="toggleDarkMode">Dark Mode</button>

  </div>
</nav>

    <div class="container mt-3">
      <div class="row">
        <!-- Current Readings -->
        <div class="col-md-4">
          <div class="card mb-3">
            <div class="card-body">
              <h5 class="card-title">Current Temperature</h5>
              <p class="card-text main-reading" id="temperature">Loading...</p>
            </div>
          </div>
        </div>
        <div class="col-md-4">
          <div class="card mb-3">
            <div class="card-body">
              <h5 class="card-title">Current Humidity</h5>
              <p class="card-text main-reading" id="humidity">Loading...</p>
            </div>
          </div>
        </div>
        <div class="col-md-4">
          <div class="card mb-3">
            <div class="card-body">
              <h5 class="card-title">Incubation Time</h5>
              <p class="card-text main-reading" id="time">Loading...</p>
            </div>
          </div>
        </div>
      </div>
      <div class="row">
        <!-- Summary Stats (Last 24h) -->
        <div class="col-md-6">
          <div class="card mb-3">
            <div class="card-body">
              <h5 class="card-title">Temperature Summary (Last 24h)</h5>
              <p class="card-text" id="tempSummary">Loading...</p>
            </div>
          </div>
        </div>
        <div class="col-md-6">
          <div class="card mb-3">
            <div class="card-body">
              <h5 class="card-title">Humidity Summary (Last 24h)</h5>
              <p class="card-text" id="humidSummary">Loading...</p>
            </div>
          </div>
        </div>
      </div>
      <div class="row">
        <!-- Timer Start Display -->
        <div class="col">
          <div class="card mb-3">
            <div class="card-body">
              <h5 class="card-title">Egg Timer Started</h5>
              <p class="card-text" id="startTimeDisplay">Loading...</p>
            </div>
          </div>
        </div>
      </div>
      <div class="row mb-3">
        <!-- Chart Time Range Buttons -->
        <div class="col text-center time-range-btns">
          <div class="btn-group btn-group-toggle" data-toggle="buttons">
            <label class="btn btn-secondary active" id="btn-24h">
              <input type="radio" name="options" checked> 24 Hours
            </label>
            <label class="btn btn-secondary" id="btn-7d">
              <input type="radio" name="options"> 7 Days
            </label>
            <label class="btn btn-secondary" id="btn-all">
              <input type="radio" name="options"> All Data
            </label>
          </div>
        </div>
      </div>
      <div class="row">
        <!-- Chart -->
        <div class="col">
          <div class="card mb-3">
            <div class="card-body">
              <canvas id="incubationChart"></canvas>
            </div>
          </div>
        </div>
      </div>
      <div class="row mb-3">
        <!-- All Time Summary: Separate boxes -->
        <div class="col-md-6">
          <div class="card">
            <div class="card-body">
              <h5 class="card-title">All Time Temperature Summary</h5>
              <p class="card-text" id="allTempSummary">Loading...</p>
            </div>
          </div>
        </div>
        <div class="col-md-6">
          <div class="card">
            <div class="card-body">
              <h5 class="card-title">All Time Humidity Summary</h5>
              <p class="card-text" id="allHumidSummary">Loading...</p>
            </div>
          </div>
        </div>
      </div>
      <!-- "Start New Batch" Button -->
      <div class="row mb-3">
        <div class="col text-center">
          <button class="btn btn-danger" id="resetBtn" title="Use this button when starting a new batch of eggs. This will reset everything: charts, start time, and all historical data.">Start New Batch</button>
        </div>
      </div>
      <!-- Update Start Time Input Group -->
      <div class="row mb-3">
        <div class="col text-center">
          <div class="input-group">
            <div class="input-group-prepend">
              <span class="input-group-text" title="Enter the number of days the eggs have been incubating">Days</span>
            </div>
            <input type="number" class="form-control" id="inputDays" placeholder="0">
            <div class="input-group-prepend">
              <span class="input-group-text" title="Enter the additional hours the eggs have been incubating">Hours</span>
            </div>
            <input type="number" class="form-control" id="inputHours" placeholder="0">
            <div class="input-group-append">
              <button class="btn btn-primary" id="updateStartBtn" title="Update the egg start time. This will clear all historical data and set a custom start time for eggs already in the incubator.">Update Start Time</button>
            </div>
          </div>
        </div>
      </div>
      <!-- Download Data JSON Button -->
      <div class="row mb-3">
        <div class="col text-center">
          <a href="/download" class="btn btn-success" role="button">
            Download Data JSON
          </a>
       <button class="btn btn-warning" id="restartBtn" title="Restart the ESP32">Restart</button>
   
        </div>
      </div>
      <!-- OTA Update Link -->
      <div class="row mb-3">
        <div class="col text-center">
          <a href="/update" class="btn btn-secondary" title="Click here to upload a new firmware .bin file">OTA Update</a>
        </div>
      </div>
    </div>
  
    <!-- JS: jQuery, Bootstrap, Chart.js -->
    <script src="https://code.jquery.com/jquery-3.5.1.slim.min.js"></script>
    <script src="https://stackpath.bootstrapcdn.com/bootstrap/4.5.2/js/bootstrap.bundle.min.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <script>
      let myChart;
      let chartData;
      let currentRange = '24h'; // Default time range
      
      let socket = new WebSocket('ws://' + window.location.hostname + '/ws');
      socket.onmessage = function(event) {
        let data = JSON.parse(event.data);
        if (data.type === "update") {
          document.getElementById('temperature').textContent = data.temperature + " °F";
          document.getElementById('humidity').textContent = data.humidity + " %";
          document.getElementById('time').textContent = data.incubationTime;
          let startDate = new Date(data.startTime * 1000);
          let options = { weekday: 'long', year: 'numeric', month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' };
          document.getElementById('startTimeDisplay').textContent = startDate.toLocaleString(undefined, options);
          if (data.summary) {
            document.getElementById('tempSummary').textContent = 
              "Avg: " + data.summary.avgTemp + " °F, Min: " + data.summary.minTemp + " °F, Max: " + data.summary.maxTemp + " °F";
            document.getElementById('humidSummary').textContent = 
              "Avg: " + data.summary.avgHumid + " %, Min: " + data.summary.minHumid + " %, Max: " + data.summary.maxHumid + " %";
          }
          if (data.allSummary) {
            document.getElementById('allTempSummary').textContent =
              "Avg: " + data.allSummary.avgTemp + " °F, Min: " + data.allSummary.minTemp + " °F, Max: " + data.allSummary.maxTemp + " °F";
            document.getElementById('allHumidSummary').textContent =
              "Avg: " + data.allSummary.avgHumid + " %, Min: " + data.allSummary.minHumid + " %, Max: " + data.allSummary.maxHumid + " %";
          }
        }
      };
      
      socket.onopen = function(event) {
        console.log("WebSocket connected.");
      };
      
      socket.onclose = function(event) {
        console.log("WebSocket disconnected.");
      };
      
      document.addEventListener('DOMContentLoaded', function() {
          fetch('/temperature')
      .then(r => r.text())
      .then(t => {
        document.getElementById('temperature').textContent = t + ' °F';
      });
    fetch('/humidity')
      .then(r => r.text())
      .then(h => {
        document.getElementById('humidity').textContent = h + ' %';
      });
    fetch('/time')
      .then(r => r.text())
      .then(tm => {
        document.getElementById('time').textContent = tm;
      });
        fetchChartData();
      document.getElementById('restartBtn').addEventListener('click', function(){
  if(confirm('Reboot the incubator ESP32?')){
    fetch('/restart')
      .then(()=>console.log('Restart command sent'))
      .catch(err=>console.error(err));
  }
});
  
        
        document.getElementById('resetBtn').addEventListener('click', function() {
          if (confirm('Are you sure you want to start a new batch of eggs? This will reset everything: charts, start time, and all historical data.')) {
            fetch('/reset')
              .then(() => { fetchChartData(); });
          }
        });
        
        document.getElementById('updateStartBtn').addEventListener('click', function() {
          let days = document.getElementById('inputDays').value;
          let hours = document.getElementById('inputHours').value;
          if (confirm('Are you sure you want to update the egg start time? This will clear all historical data and set a custom start time for eggs already in the incubator.')) {
            fetch('/setstarttime?days=' + days + '&hours=' + hours)
              .then(response => response.text())
              .then(result => {
                alert(result);
                fetchChartData();
              })
              .catch(err => console.error(err));
          }
        });
        
        document.getElementById('toggleDarkMode').addEventListener('click', function() {
          document.body.classList.toggle('dark-mode');
        });
        document.getElementById('btn-24h').addEventListener('click', function() {
          currentRange = '24h';
          updateChart();
          document.querySelectorAll('.time-range-btns .btn').forEach(btn => btn.classList.remove('active'));
          this.classList.add('active');
        });
        document.getElementById('btn-7d').addEventListener('click', function() {
          currentRange = '7d';
          updateChart();
          document.querySelectorAll('.time-range-btns .btn').forEach(btn => btn.classList.remove('active'));
          this.classList.add('active');
        });
        document.getElementById('btn-all').addEventListener('click', function() {
          currentRange = 'all';
          updateChart();
          document.querySelectorAll('.time-range-btns .btn').forEach(btn => btn.classList.remove('active'));
          this.classList.add('active');
        });
        
        setInterval(fetchChartData, 60000);
      });
      
      function updateChart() {
        if (!chartData || chartData.length === 0) return;
        let filteredData = filterDataByRange(chartData, currentRange);
        // Filter out invalid readings (temperature > 0) and sort chronologically
        let validData = filteredData.filter(point => point.temperature > 0);
        validData.sort((a, b) => a.timestamp - b.timestamp);
        
        const labels = validData.map(point => {
          const date = new Date(point.timestamp * 1000);
          let timeStr = date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', hour12: true });
          let dateStr = date.toLocaleDateString([], { month: 'short', day: 'numeric' });
          return timeStr + ' ' + dateStr;
        });
        const tempData = validData.map(point => point.temperature);
        const humidData = validData.map(point => point.humidity);
        
        const validTemps = tempData.filter(v => v > 0);
        const tempMin = validTemps.length > 0 ? Math.min(...validTemps) : 0;
        const tempMax = validTemps.length > 0 ? Math.max(...validTemps) : 100;
        const chartConfig = {
          type: 'line',
          data: {
            labels: labels,
            datasets: [
              {
                label: 'Temperature (°F)',
                data: tempData,
                borderColor: 'rgb(255, 99, 132)',
                backgroundColor: 'rgba(255, 99, 132, 0.1)',
                tension: 0.1,
                yAxisID: 'y'
              },
              {
                label: 'Humidity (%)',
                data: humidData,
                borderColor: 'rgb(54, 162, 235)',
                backgroundColor: 'rgba(54, 162, 235, 0.1)',
                tension: 0.1,
                yAxisID: 'y1'
              }
            ]
          },
          options: {
            responsive: true,
            interaction: { mode: 'index', intersect: false },
            scales: {
              x: { ticks: { maxRotation: 45, minRotation: 45 } },
              y: {
                type: 'linear',
                display: true,
                position: 'left',
                title: { display: true, text: 'Temperature (°F)' },
                min: tempMin - 5,
                max: tempMax + 5
              },
              y1: {
                type: 'linear',
                display: true,
                position: 'right',
                title: { display: true, text: 'Humidity (%)' },
                min: 0,
                max: 100,
                grid: { drawOnChartArea: false }
              }
            }
          }
        };
        if (myChart) { myChart.destroy(); }
        const ctx = document.getElementById('incubationChart').getContext('2d');
        myChart = new Chart(ctx, chartConfig);
      }
      
      function filterDataByRange(data, range) {
        const now = new Date().getTime() / 1000;
        switch(range) {
          case '24h': return data.filter(point => point.timestamp > now - 86400);
          case '7d': return data.filter(point => point.timestamp > now - 604800);
          case 'all': default: return data;
        }
      }
      
      function fetchChartData() {
        fetch('/data')
          .then(response => response.json())
          .then(data => {
            chartData = data;
            updateChart();
          })
          .catch(error => console.error('Error fetching chart data:', error));
      }
    </script>
  </body>
</html>
)rawliteral";

// --- Setup Function ---
void setup() {
  Serial.begin(115200);
  Serial.println("Starting setup...");

  // Initialize sensor
  dht.begin();
  delay(2000);
  Serial.println("DHT sensor initialized");

  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("An error occurred while mounting SPIFFS");
    return;
  }
  Serial.println("SPIFFS mounted");

  // —— SPIFFS Debug: list every file in SPIFFS ——
  Serial.println("Listing SPIFFS contents:");
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file) {
    Serial.printf("  %s (size: %u bytes)\n", file.name(), file.size());
    file = root.openNextFile();
  }
  Serial.println("End of SPIFFS listing");

  // Load saved data points from SPIFFS
  loadDataFromFile();
  
  Serial.printf("Free heap before WiFi: %d bytes\n", ESP.getFreeHeap());
  
  // Initialize Preferences and preserve the incubation start time
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
  
  // Set up WiFiManager
  WiFiManager wifiManager;
  wifiManager.setAPStaticIPConfig(IPAddress(192,168,4,1),
                                  IPAddress(192,168,4,1),
                                  IPAddress(255,255,255,0));
  Serial.println("Attempting WiFi connection...");
  wifiManager.autoConnect("EggTimer-Setup");
  Serial.println("WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Initialize NTP client with raw UTC (offset 0)
  timeClient.begin();
  timeClient.setTimeOffset(0);
  Serial.println("NTP client started");

  // Set up mDNS
  if (MDNS.begin("eggtimer2")) {
    Serial.println("MDNS responder started");
  } else {
    Serial.println("Error setting up MDNS responder!");
  }

  // Set up OTA update page using ElegantOTA
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
  delay(100);           // give the client time to receive
  ESP.restart();
});

  // Serve /data.json as an attachment (triggers browser “Save As…”)
  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request){
    // the last `true` tells AsyncWebServer to send it as an attachment
    request->send(SPIFFS, "/data.json", "application/json", true);
  });

  // Define HTTP routes
  server.on("/test", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Server is running!");
  });
  server.on("/setstarttime", HTTP_GET, handleSetStartTime);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("Root page requested");
    request->send(200, "text/html", index_html);
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
  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("Timer reset requested");
    resetIncubationTimer();
    request->send(200, "text/plain", "Timer and all data reset");
  });
  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("Chart data requested");
    request->send(200, "application/json", getDataJSON());
  });

  // Just before server.begin();
server.serveStatic("/favicon.ico", SPIFFS, "/favicon.ico").setCacheControl("max-age=86400");



  Serial.printf("Free heap after server setup: %d bytes\n", ESP.getFreeHeap());
  server.begin();
  Serial.println("Web server started!");
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    timeClient.update();
    unsigned long currentEpoch = timeClient.getEpochTime();

    // Debug: Print current epoch, last logged epoch, and the time difference.
    // Serial.printf("Current Epoch: %lu, lastDataLogTime: %lu, Difference: %lu\n",
    //               currentEpoch, lastDataLogTime, currentEpoch - lastDataLogTime);

    // Log data point every 3600 seconds (1 hour)
    if (currentEpoch > 1600000000 &&
        (lastDataLogTime == 0 || currentEpoch - lastDataLogTime >= 3600)) {
      logDataPoint();
      lastDataLogTime = currentEpoch;
      Serial.println("Data point logged");
    }
    
    // Update sensor readings every minute
    static unsigned long lastSensorUpdate = 0;
    if (millis() - lastSensorUpdate > 60000) {
      float newTemp = dht.readTemperature(true); // Fahrenheit
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
