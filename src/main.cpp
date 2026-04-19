// Original source code: https://wiki.keyestudio.com/KS0429_keyestudio_TDS_Meter_V1.0#Test_Code
// Project details: https://RandomNerdTutorials.com/esp32-tds-water-quality-sensor/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

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

enum MotorMode { AUTO, FORCE_ON, FORCE_OFF };
MotorMode motorMode = AUTO;

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

bool motorIsRunning() {
  if (motorMode == FORCE_ON)  return true;
  if (motorMode == FORCE_OFF) return false;
  
  // AUTO mode: run motor if TDS is above threshold
  return (tdsValue > TDS_THRESHOLD);
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

  String html = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Hydroponics Monitor</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { font-family: Arial, sans-serif; background: #1a1a2e; color: #eee;
           display: flex; flex-direction: column; align-items: center; padding: 30px; }
    h1 { color: #00d4ff; margin-bottom: 20px; }
    .card { background: #16213e; border-radius: 12px; padding: 24px; margin: 10px;
            width: 300px; text-align: center; box-shadow: 0 4px 15px rgba(0,0,0,0.3); }
    .label { font-size: 0.9em; color: #aaa; margin-bottom: 8px; }
    .tds-value { font-size: 3.5em; font-weight: bold; color: #00d4ff; }
    .tds-unit { font-size: 1em; color: #aaa; margin-top: 4px; }
    .motor-status { font-size: 1.6em; font-weight: bold; margin: 8px 0; }
    .running { color: #00e676; }
    .stopped { color: #ff5252; }
    .mode-label { font-size: 0.85em; color: #aaa; }
    .btn { display: inline-block; margin: 6px; padding: 11px 22px; border: none;
           border-radius: 8px; font-size: 0.95em; cursor: pointer;
           text-decoration: none; color: white; font-weight: bold; }
    .btn-on   { background: #00897b; }
    .btn-off  { background: #c62828; }
    .btn-auto { background: #1565c0; }
  </style>
  <script>
    function fetchStatus() {
      fetch('/api/status')
        .then(r => r.json())
        .then(d => {
          document.getElementById('tds').textContent = d.tds;
          var s = document.getElementById('motor-status');
          s.textContent = d.running ? 'Running' : 'Stopped';
          s.className = 'motor-status ' + (d.running ? 'running' : 'stopped');
          document.getElementById('mode').textContent = 'Mode: ' + d.mode;
        })
        .catch(() => {});
    }
    setInterval(fetchStatus, 1500);
    window.onload = fetchStatus;
  </script>
</head>
<body>
  <h1>Hydroponics Monitor</h1>

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
  </div>

  <div class="card">
    <div class="label">Motor Override</div>
    <br>
    <a class="btn btn-on"   href="/motor/on">Force ON</a>
    <a class="btn btn-off"  href="/motor/off">Force OFF</a>
    <a class="btn btn-auto" href="/motor/auto">Auto</a>
    <div style="margin-top: 15px; font-size: 0.8em; color: #aaa;">
      Auto mode: Motor runs if TDS &gt; 15 ppm
    </div>
  </div>
</body>
</html>)rawliteral";

  server.send(200, "text/html", html);
}

void handleStatus() {
  bool running = motorIsRunning();
  String modeStr = (motorMode == FORCE_ON)  ? "Override ON"  :
                   (motorMode == FORCE_OFF) ? "Override OFF" : "Auto";
  String json = "{\"tds\":" + String((int)tdsValue) +
                ",\"running\":" + (running ? "true" : "false") +
                ",\"mode\":\"" + modeStr + "\"}";
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

void setup() {
  Serial.begin(115200);
  pinMode(TdsSensorPin, INPUT);
  pinMode(MOT_IN1, OUTPUT);
  pinMode(MOT_IN2, OUTPUT);
  digitalWrite(MOT_IN1, LOW);
  digitalWrite(MOT_IN2, LOW);

  Serial.println("\n=== Hydroponics Controller Starting ===");
  Serial.println("TDS Threshold:");
  Serial.print("  Motor runs if TDS > ");
  Serial.print(TDS_THRESHOLD);
  Serial.println(" ppm");
  Serial.println();
  
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

  server.on("/",           handleRoot);
  server.on("/api/status", handleStatus);
  server.on("/motor/on",   handleMotorOn);
  server.on("/motor/off",  handleMotorOff);
  server.on("/motor/auto", handleMotorAuto);
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
      if (tdsValue > TDS_THRESHOLD) {
        Serial.println("TDS above threshold, motor ON");
      } else {
        Serial.println("TDS below threshold, motor OFF");
      }
    } else if (motorMode == FORCE_ON) {
      Serial.println("FORCE ON");
    } else {
      Serial.println("FORCE OFF");
    }

    setMotor(motorIsRunning());
  }
}
