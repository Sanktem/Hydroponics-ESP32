// Original source code: https://wiki.keyestudio.com/KS0429_keyestudio_TDS_Meter_V1.0#Test_Code
// Project details: https://RandomNerdTutorials.com/esp32-tds-water-quality-sensor/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>

// --- WiFi credentials (fill these in) ---
const char* WIFI_SSID = "EscapedNigerianPrince";
const char* WIFI_PASS = "bbroygbvgw......";

#define TdsSensorPin 34  // Must be an ADC1 pin (GPIO32-39) — ADC2 conflicts with WiFi
#define VREF 3.3
#define SCOUNT 30

#define MOT_IN1 26
#define MOT_IN2 25

// TDS threshold for automatic control
#define TDS_THRESHOLD 15  // ppm - above this, motor runs

// Default timing constants (user-configurable)
unsigned long pumpOnDuration = 30000;   // 30 seconds ON
unsigned long pumpOffDuration = 300000; // 5 minutes OFF

// Schedule settings (user-configurable)
int wateringStartHour = 6;  // 06:00
int wateringEndHour = 18;   // 18:00

enum MotorMode { AUTO, FORCE_ON, FORCE_OFF };
MotorMode motorMode = AUTO;

// Timing variables
unsigned long lastPumpStateChange = 0;
bool pumpShouldBeOn = false;

// Time zone settings
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -6*3600;     // Adjust for your timezone
const int   daylightOffset_sec = 0; // Adjust for daylight saving

int analogBuffer[SCOUNT];
int analogBufferTemp[SCOUNT];
int analogBufferIndex = 0;
int copyIndex = 0;

float averageVoltage = 0;
float tdsValue = 0;
float temperature = 25;

WebServer server(80);

// median filtering algorithm
int getMedianNum(int bArray[], int iFilterLen) {
  int bTab[iFilterLen];
  for (byte i = 0; i < iFilterLen; i++)
    bTab[i] = bArray[i];
  int i, j, bTemp;
  for (j = 0; j < iFilterLen - 1; j++) {
    for (i = 0; i < iFilterLen - j - 1; i++) {
      if (bTab[i] > bTab[i + 1]) {
        bTemp = bTab[i];
        bTab[i] = bTab[i + 1];
        bTab[i + 1] = bTemp;
      }
    }
  }
  if ((iFilterLen & 1) > 0)
    bTemp = bTab[(iFilterLen - 1) / 2];
  else
    bTemp = (bTab[iFilterLen / 2] + bTab[iFilterLen / 2 - 1]) / 2;
  return bTemp;
}

String getCurrentTime() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    return "Time not set";
  }
  char timeStringBuff[50];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M:%S", &timeinfo);
  return String(timeStringBuff);
}

bool isWithinWateringHours() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    return true; // If time not available, allow watering
  }
  
  int currentHour = timeinfo.tm_hour;
  
  // Handle case where end hour is next day (e.g., 22:00 to 06:00)
  if (wateringStartHour <= wateringEndHour) {
    return (currentHour >= wateringStartHour && currentHour < wateringEndHour);
  } else {
    return (currentHour >= wateringStartHour || currentHour < wateringEndHour);
  }
}

bool motorIsRunning() {
  if (motorMode == FORCE_ON)  return true;
  if (motorMode == FORCE_OFF) return false;
  
  // AUTO mode: check TDS first (safety - don't run without water)
  if (tdsValue <= TDS_THRESHOLD) {
    pumpShouldBeOn = false;
    lastPumpStateChange = millis(); // Reset timing when no water
    return false;
  }
  
  // Check if we're within watering hours
  if (!isWithinWateringHours()) {
    pumpShouldBeOn = false;
    lastPumpStateChange = millis(); // Reset timing when outside hours
    return false;
  }
  
  // If we have adequate water and within hours, use interval timing
  unsigned long currentTime = millis();
  unsigned long timeSinceChange = currentTime - lastPumpStateChange;
  
  if (pumpShouldBeOn) {
    // Pump is currently in ON cycle
    if (timeSinceChange >= pumpOnDuration) {
      pumpShouldBeOn = false;
      lastPumpStateChange = currentTime;
    }
  } else {
    // Pump is currently in OFF cycle
    if (timeSinceChange >= pumpOffDuration) {
      pumpShouldBeOn = true;
      lastPumpStateChange = currentTime;
    }
  }
  
  return pumpShouldBeOn;
}

void setMotor(bool on) {
  digitalWrite(MOT_IN1, on ? HIGH : LOW);
  digitalWrite(MOT_IN2, LOW);
  
  // Debug output
  Serial.print("Motor: ");
  Serial.println(on ? "ON" : "OFF");
}

void handleRoot() {
  String modeStr = (motorMode == FORCE_ON)  ? "Override ON"  :
                   (motorMode == FORCE_OFF) ? "Override OFF" : "Auto";
  bool running = motorIsRunning();
  
  // Calculate timing info for AUTO mode
  String timingInfo = "";
  if (motorMode == AUTO && tdsValue > TDS_THRESHOLD && isWithinWateringHours()) {
    unsigned long timeSinceChange = millis() - lastPumpStateChange;
    unsigned long timeRemaining;
    
    if (pumpShouldBeOn) {
      timeRemaining = (pumpOnDuration - timeSinceChange) / 1000;
      timingInfo = "ON for " + String(timeRemaining) + "s more";
    } else {
      timeRemaining = (pumpOffDuration - timeSinceChange) / 1000;
      timingInfo = "OFF for " + String(timeRemaining) + "s more";
    }
  } else if (motorMode == AUTO && !isWithinWateringHours()) {
    timingInfo = "Outside watering hours";
  }

  String html = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Hydroponics Monitor</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { font-family: Arial, sans-serif; background: #1a1a2e; color: #eee;
           display: flex; flex-direction: column; align-items: center; padding: 20px; }
    h1 { color: #00d4ff; margin-bottom: 20px; }
    .card { background: #16213e; border-radius: 12px; padding: 20px; margin: 8px;
            width: 320px; text-align: center; box-shadow: 0 4px 15px rgba(0,0,0,0.3); }
    .label { font-size: 0.9em; color: #aaa; margin-bottom: 8px; }
    .tds-value { font-size: 3.5em; font-weight: bold; color: #00d4ff; }
    .tds-unit { font-size: 1em; color: #aaa; margin-top: 4px; }
    .motor-status { font-size: 1.6em; font-weight: bold; margin: 8px 0; }
    .running { color: #00e676; }
    .stopped { color: #ff5252; }
    .mode-label { font-size: 0.85em; color: #aaa; }
    .timing-info { font-size: 0.9em; color: #ffeb3b; margin-top: 8px; }
    .time-display { font-size: 1.1em; color: #00d4ff; margin-bottom: 10px; }
    .btn { display: inline-block; margin: 4px; padding: 8px 16px; border: none;
           border-radius: 6px; font-size: 0.85em; cursor: pointer;
           text-decoration: none; color: white; font-weight: bold; }
    .btn-on   { background: #00897b; }
    .btn-off  { background: #c62828; }
    .btn-auto { background: #1565c0; }
    .btn-save { background: #ff9800; }
    .settings-row { margin: 8px 0; display: flex; align-items: center; justify-content: space-between; }
    .settings-label { font-size: 0.85em; color: #aaa; }
    input[type="number"] { width: 70px; padding: 4px; border: 1px solid #555;
                          border-radius: 4px; background: #2a2a2a; color: #eee; }
    input[type="time"] { padding: 4px; border: 1px solid #555;
                        border-radius: 4px; background: #2a2a2a; color: #eee; }
  </style>
  <script>
    function fetchStatus() {
      fetch('/api/status')
        .then(r => r.json())
        .then(d => {
          document.getElementById('tds').textContent = d.tds;
          document.getElementById('current-time').textContent = d.time;
          var s = document.getElementById('motor-status');
          s.textContent = d.running ? 'Running' : 'Stopped';
          s.className = 'motor-status ' + (d.running ? 'running' : 'stopped');
          document.getElementById('mode').textContent = 'Mode: ' + d.mode;
          
          // Update timing info
          var timingEl = document.getElementById('timing-info');
          if (d.timing && d.timing !== '') {
            timingEl.textContent = d.timing;
            timingEl.style.display = 'block';
          } else {
            timingEl.style.display = 'none';
          }
        })
        .catch(() => {});
    }
    
    function saveSettings() {
      const onDuration = document.getElementById('on-duration').value;
      const offDuration = document.getElementById('off-duration').value;
      const startTime = document.getElementById('start-time').value;
      const endTime = document.getElementById('end-time').value;
      
      fetch('/settings', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `onDuration=${onDuration}&offDuration=${offDuration}&startTime=${startTime}&endTime=${endTime}`
      }).then(() => {
        alert('Settings saved!');
        fetchStatus();
      });
    }
    
    setInterval(fetchStatus, 2000);
    window.onload = fetchStatus;
  </script>
</head>
<body>
  <h1>Hydroponics Monitor</h1>
  
  <div class="time-display" id="current-time">)rawliteral";
  html += getCurrentTime();
  html += R"rawliteral(</div>

  <div class="card">
    <div class="label">TDS Value</div>
    <div class="tds-value" id="tds">)rawliteral";
  html += String((int)tdsValue);
  html += R"rawliteral(</div>
    <div class="tds-unit">ppm</div>
  </div>

  <div class="card">
    <div class="label">Water Pump</div>
    <div class="motor-status )rawliteral";
  html += running ? "running" : "stopped";
  html += R"rawliteral(" id="motor-status">)rawliteral";
  html += running ? "Running" : "Stopped";
  html += R"rawliteral(</div>
    <div class="mode-label" id="mode">Mode: )rawliteral";
  html += modeStr;
  html += R"rawliteral(</div>
    <div class="timing-info" id="timing-info" style="display:)rawliteral";
  html += (timingInfo != "") ? "block" : "none";
  html += R"rawliteral(;">)rawliteral";
  html += timingInfo;
  html += R"rawliteral(</div>
  </div>

  <div class="card">
    <div class="label">Motor Control</div>
    <br>
    <a class="btn btn-on"   href="/motor/on">Force ON</a>)rawliteral";
  html += R"rawliteral(
    <a class="btn btn-off"  href="/motor/off">Force OFF</a>
    <a class="btn btn-auto" href="/motor/auto">Auto</a>
  </div>
  
  <div class="card">
    <div class="label">Pump Timing Settings</div>
    <div class="settings-row">
      <span class="settings-label">ON Duration (sec):</span>
      <input type="number" id="on-duration" value=")rawliteral";
  html += String(pumpOnDuration/1000);
  html += R"rawliteral(" min="5" max="300">
    </div>
    <div class="settings-row">
      <span class="settings-label">OFF Duration (sec):</span>
      <input type="number" id="off-duration" value=")rawliteral";
  html += String(pumpOffDuration/1000);
  html += R"rawliteral(" min="30" max="3600">
    </div>
    <br>
    <div class="label">Daily Schedule</div>
    <div class="settings-row">
      <span class="settings-label">Start Time:</span>
      <input type="time" id="start-time" value=")rawliteral";
  html += String(wateringStartHour < 10 ? "0" : "") + String(wateringStartHour) + ":00";
  html += R"rawliteral(">
    </div>
    <div class="settings-row">
      <span class="settings-label">End Time:</span>
      <input type="time" id="end-time" value=")rawliteral";
  html += String(wateringEndHour < 10 ? "0" : "") + String(wateringEndHour) + ":00";
  html += R"rawliteral(">
    </div>
    <br>
    <button class="btn btn-save" onclick="saveSettings()">Save Settings</button>
  </div>
</body>
</html>)rawliteral";

  server.send(200, "text/html", html);
}

void handleStatus() {
  bool running = motorIsRunning();
  String modeStr = (motorMode == FORCE_ON)  ? "Override ON"  :
                   (motorMode == FORCE_OFF) ? "Override OFF" : "Auto";
  
  // Calculate timing info for AUTO mode
  String timingInfo = "";
  if (motorMode == AUTO && tdsValue > TDS_THRESHOLD && isWithinWateringHours()) {
    unsigned long timeSinceChange = millis() - lastPumpStateChange;
    unsigned long timeRemaining;
    
    if (pumpShouldBeOn) {
      timeRemaining = (pumpOnDuration - timeSinceChange) / 1000;
      timingInfo = "ON for " + String(timeRemaining) + "s more";
    } else {
      timeRemaining = (pumpOffDuration - timeSinceChange) / 1000;
      timingInfo = "OFF for " + String(timeRemaining) + "s more";
    }
  } else if (motorMode == AUTO && !isWithinWateringHours()) {
    timingInfo = "Outside watering hours";
  }
  
  String json = "{\"tds\":" + String((int)tdsValue) +
                ",\"running\":" + (running ? "true" : "false") +
                ",\"mode\":\"" + modeStr + "\"" +
                ",\"timing\":\"" + timingInfo + "\"" +
                ",\"time\":\"" + getCurrentTime() + "\"}";
  server.send(200, "application/json", json);
}

void handleMotorOn() {
  motorMode = FORCE_ON;
  setMotor(true);  // Immediately turn on motor
  Serial.println("Web request: Motor FORCE ON");
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleMotorOff() {
  motorMode = FORCE_OFF;
  setMotor(false);  // Immediately turn off motor
  Serial.println("Web request: Motor FORCE OFF");
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleMotorAuto() {
  motorMode = AUTO;
  Serial.println("Web request: Motor AUTO mode");
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSettings() {
  if (server.method() == HTTP_POST) {
    // Parse settings from POST data
    String onDurationStr = server.arg("onDuration");
    String offDurationStr = server.arg("offDuration");
    String startTimeStr = server.arg("startTime");
    String endTimeStr = server.arg("endTime");
    
    // Update timing settings
    pumpOnDuration = onDurationStr.toInt() * 1000; // Convert to milliseconds
    pumpOffDuration = offDurationStr.toInt() * 1000;
    
    // Parse time strings (format: "HH:MM")
    int startHour = startTimeStr.substring(0, 2).toInt();
    int endHour = endTimeStr.substring(0, 2).toInt();
    
    wateringStartHour = startHour;
    wateringEndHour = endHour;
    
    // Reset pump cycle timing when settings change
    lastPumpStateChange = millis();
    pumpShouldBeOn = false;
    
    Serial.println("Settings updated:");
    Serial.print("  ON duration: "); Serial.print(pumpOnDuration/1000); Serial.println("s");
    Serial.print("  OFF duration: "); Serial.print(pumpOffDuration/1000); Serial.println("s");
    Serial.print("  Watering hours: "); Serial.print(wateringStartHour); 
    Serial.print(":00 to "); Serial.print(wateringEndHour); Serial.println(":00");
    
    server.send(200, "text/plain", "Settings saved");
  } else {
    server.send(405, "text/plain", "Method not allowed");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(TdsSensorPin, INPUT);
  pinMode(MOT_IN1, OUTPUT);
  pinMode(MOT_IN2, OUTPUT);
  digitalWrite(MOT_IN1, LOW);
  digitalWrite(MOT_IN2, LOW);

  Serial.println("\n=== Hydroponics Controller Starting ===");
  Serial.println("TDS Threshold and Timing:");
  Serial.print("  Motor runs if TDS > ");
  Serial.print(TDS_THRESHOLD);
  Serial.println(" ppm");
  Serial.print("  Pump cycles: ");
  Serial.print(pumpOnDuration/1000);
  Serial.print("s ON, ");
  Serial.print(pumpOffDuration/1000);
  Serial.println("s OFF");
  Serial.print("  Watering hours: ");
  Serial.print(wateringStartHour);
  Serial.print(":00 to ");
  Serial.print(wateringEndHour);
  Serial.println(":00");
  Serial.println();
  
  // Initialize timing
  lastPumpStateChange = millis();
  pumpShouldBeOn = false;
  
  // Test motor during startup
  Serial.println("Testing motor...");
  setMotor(true);
  delay(1000);
  setMotor(false);
  Serial.println("Motor test complete.\n");

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Web server ready at http://");
  Serial.println(WiFi.localIP());
  
  // Initialize NTP time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("NTP time sync initiated...");

  server.on("/",           handleRoot);
  server.on("/api/status", handleStatus);
  server.on("/motor/on",   handleMotorOn);
  server.on("/motor/off",  handleMotorOff);
  server.on("/motor/auto", handleMotorAuto);
  server.on("/settings",   HTTP_POST, handleSettings);
  server.begin();
}

void loop() {
  server.handleClient();

  static unsigned long analogSampleTimepoint = millis();
  if (millis() - analogSampleTimepoint > 40U) {
    analogSampleTimepoint = millis();
    analogBuffer[analogBufferIndex] = analogRead(TdsSensorPin);
    analogBufferIndex++;
    if (analogBufferIndex == SCOUNT)
      analogBufferIndex = 0;
  }

  static unsigned long printTimepoint = millis();
  if (millis() - printTimepoint > 800U) {
    printTimepoint = millis();
    for (copyIndex = 0; copyIndex < SCOUNT; copyIndex++)
      analogBufferTemp[copyIndex] = analogBuffer[copyIndex];

    averageVoltage = getMedianNum(analogBufferTemp, SCOUNT) * (float)VREF / 4096.0;
    float compensationCoefficient = 1.0 + 0.02 * (temperature - 25.0);
    float compensationVoltage = averageVoltage / compensationCoefficient;
    tdsValue = (133.42 * compensationVoltage * compensationVoltage * compensationVoltage
               - 255.86 * compensationVoltage * compensationVoltage
               + 857.39 * compensationVoltage) * 0.5;

    Serial.print("TDS Value:");
    Serial.print(tdsValue, 0);
    Serial.println("ppm");
    
    // Show motor control logic
    Serial.print("Motor Mode: ");
    if (motorMode == AUTO) {
      Serial.print("AUTO - ");
      if (tdsValue <= TDS_THRESHOLD) {
        Serial.println("TDS below threshold, motor OFF");
      } else if (!isWithinWateringHours()) {
        Serial.println("Outside watering hours, motor OFF");
      } else {
        Serial.print("TDS OK, in watering hours, cycle: ");
        unsigned long timeSinceChange = millis() - lastPumpStateChange;
        if (pumpShouldBeOn) {
          Serial.print("ON (");
          Serial.print((pumpOnDuration - timeSinceChange) / 1000);
          Serial.println("s remaining)");
        } else {
          Serial.print("OFF (");
          Serial.print((pumpOffDuration - timeSinceChange) / 1000);
          Serial.println("s remaining)");
        }
      }
    } else if (motorMode == FORCE_ON) {
      Serial.println("FORCE ON");
    } else {
      Serial.println("FORCE OFF");
    }

    setMotor(motorIsRunning());
  }
}
