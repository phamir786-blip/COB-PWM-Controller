#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include "config.h"

WebServer server(HTTP_PORT);
Preferences prefs;
bool powerOn = true;
uint8_t brightness = DEFAULT_BRIGHTNESS;
uint32_t lastWifiAttempt = 0;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 10000;

static uint32_t dutyForBrightness(uint8_t value) {
  const uint32_t maxDuty = (1UL << PWM_RESOLUTION_BITS) - 1UL;
  return (static_cast<uint32_t>(value) * maxDuty) / 100UL;
}

void applyOutput() {
  ledcWrite(PWM_PIN, powerOn ? dutyForBrightness(brightness) : 0);
}

void saveState() {
  prefs.putBool("power", powerOn);
  prefs.putUChar("brightness", brightness);
}

void loadState() {
  powerOn = prefs.getBool("power", true);
  brightness = prefs.getUChar("brightness", DEFAULT_BRIGHTNESS);
  if (brightness < BRIGHTNESS_MIN || brightness > BRIGHTNESS_MAX) brightness = DEFAULT_BRIGHTNESS;
}

void handleRoot() {
  const String html = R"HTML(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>COB PWM Controller</title>
<style>body{font-family:system-ui;margin:0;padding:24px;background:#111;color:#eee}.card{max-width:520px;margin:auto;padding:24px;border-radius:18px;background:#1c1c1c}button,input{font:inherit}button{padding:12px 18px;border:0;border-radius:12px;margin:4px}input[type=range]{width:100%}.value{font-size:2rem;font-weight:700;margin:12px 0}</style>
</head><body><div class="card"><h1>COB PWM Controller</h1><p>ESP32-C3 • GPIO 3 • 20 kHz PWM</p>
<div class="value"><span id="v">--</span>%</div><input id="b" type="range" min="1" max="100" value="50">
<div><button onclick="setPower(1)">ON</button><button onclick="setPower(0)">OFF</button></div><p id="s">Loading...</p></div>
<script>
const b=document.getElementById('b'),v=document.getElementById('v'),s=document.getElementById('s');
async function state(){const r=await fetch('/api/state');const x=await r.json();b.value=x.brightness;v.textContent=x.brightness;s.textContent=x.power?'ON':'OFF';}
b.oninput=()=>v.textContent=b.value;
b.onchange=()=>fetch('/api/brightness?value='+b.value,{method:'POST'}).then(state);
function setPower(x){fetch('/api/power?on='+x,{method:'POST'}).then(state);}
state();
</script></body></html>)HTML";
  server.send(200, "text/html", html);
}

void handleState() {
  String json = "{\"power\":" + String(powerOn ? "true" : "false") +
                ",\"brightness\":" + String(brightness) +
                ",\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") +
                ",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
  server.send(200, "application/json", json);
}

void handleBrightness() {
  if (!server.hasArg("value")) { server.send(400, "text/plain", "Missing value"); return; }
  int value = server.arg("value").toInt();
  if (value < BRIGHTNESS_MIN || value > BRIGHTNESS_MAX) {
    server.send(400, "text/plain", "Brightness must be 1..100"); return;
  }
  brightness = static_cast<uint8_t>(value);
  applyOutput();
  saveState();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handlePower() {
  if (!server.hasArg("on")) { server.send(400, "text/plain", "Missing on"); return; }
  powerOn = server.arg("on") == "1" || server.arg("on") == "true";
  applyOutput();
  saveState();
  server.send(200, "application/json", "{\"ok\":true}");
}

void startServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/brightness", HTTP_POST, handleBrightness);
  server.on("/api/power", HTTP_POST, handlePower);
  server.begin();
}

void connectWiFi() {
  if (strlen(WIFI_SSID) == 0) {
    Serial.println("Wi-Fi SSID is empty. Set WIFI_SSID/WIFI_PASSWORD in include/config.h.");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiAttempt = millis();
  Serial.print("Connecting to Wi-Fi");
  for (uint8_t i = 0; i < 20 && WiFi.status() != WL_CONNECTED; ++i) { delay(250); Serial.print('.'); }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    ArduinoOTA.setHostname("cob-pwm");
    ArduinoOTA.begin();
  } else {
    Serial.println("Wi-Fi connection not established.");
  }
}

void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  uint32_t now = millis();
  if (now - lastWifiAttempt < WIFI_RETRY_INTERVAL_MS) return;
  lastWifiAttempt = now;
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void setup() {
  Serial.begin(115200);
  delay(50);
  // No Bluetooth initialization. No OLED/display code. No deep/light sleep.
  if (!ledcAttach(PWM_PIN, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS)) {
    Serial.println("ERROR: PWM attach failed.");
    while (true) delay(1000);
  }
  prefs.begin("cob-pwm", false);
  loadState();
  applyOutput();
  connectWiFi();
  startServer();
  Serial.println("COB PWM Controller ready.");
  Serial.printf("Brightness: %u%%, Power: %s\n", brightness, powerOn ? "ON" : "OFF");
}

void loop() {
  server.handleClient();
  if (WiFi.status() == WL_CONNECTED) ArduinoOTA.handle();
  else maintainWiFi();
}
