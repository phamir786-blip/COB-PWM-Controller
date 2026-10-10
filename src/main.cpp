#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <ArduinoIoTCloud.h>
#include <Arduino_ConnectionHandler.h>
#include "config.h"

#if __has_include("arduino_cloud_secrets.h")
#include "arduino_cloud_secrets.h"
#else
#define ARDUINO_CLOUD_DEVICE_ID "6e99597d-ac82-44f9-8409-735fea959de0"
#define ARDUINO_CLOUD_DEVICE_KEY ""
#define ARDUINO_CLOUD_WIFI_SSID ""
#define ARDUINO_CLOUD_WIFI_PASSWORD ""
#endif

WebServer server(HTTP_PORT);
Preferences prefs;
bool powerOn = true;
int brightness = DEFAULT_BRIGHTNESS;
uint32_t lastWifiAttempt = 0;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
constexpr uint8_t PWM_CHANNEL = 0;
bool cloudEnabled = false;
bool otaStarted = false;

WiFiConnectionHandler ArduinoIoTPreferredConnection(
  ARDUINO_CLOUD_WIFI_SSID,
  ARDUINO_CLOUD_WIFI_PASSWORD
);

static uint32_t dutyForBrightness(uint8_t value) {
  const uint32_t maxDuty = (1UL << PWM_RESOLUTION_BITS) - 1UL;
  return (static_cast<uint32_t>(value) * maxDuty) / 100UL;
}

void applyOutput() {
  const uint8_t safeBrightness = static_cast<uint8_t>(
    brightness < BRIGHTNESS_MIN ? BRIGHTNESS_MIN :
    brightness > BRIGHTNESS_MAX ? BRIGHTNESS_MAX : brightness
  );
  ledcWrite(PWM_CHANNEL, powerOn ? dutyForBrightness(safeBrightness) : 0);
}

void saveState() {
  prefs.putBool("power", powerOn);
  prefs.putUChar("brightness", static_cast<uint8_t>(brightness));
}

void loadState() {
  powerOn = prefs.getBool("power", true);
  brightness = prefs.getUChar("brightness", DEFAULT_BRIGHTNESS);
  if (brightness < BRIGHTNESS_MIN || brightness > BRIGHTNESS_MAX) brightness = DEFAULT_BRIGHTNESS;
}

void onPowerOnChange() {
  applyOutput();
  saveState();
}

void onBrightnessChange() {
  if (brightness < BRIGHTNESS_MIN) brightness = BRIGHTNESS_MIN;
  if (brightness > BRIGHTNESS_MAX) brightness = BRIGHTNESS_MAX;
  applyOutput();
  saveState();
}

bool hasArduinoCloudCredentials() {
  return strlen(ARDUINO_CLOUD_DEVICE_ID) > 0 &&
         strlen(ARDUINO_CLOUD_DEVICE_KEY) > 0 &&
         strlen(ARDUINO_CLOUD_WIFI_SSID) > 0 &&
         strlen(ARDUINO_CLOUD_WIFI_PASSWORD) > 0;
}

void startOTAIfNeeded() {
  if (otaStarted || WiFi.status() != WL_CONNECTED) return;
  ArduinoOTA.setHostname("cob-pwm");
  ArduinoOTA.begin();
  otaStarted = true;
  Serial.println("ArduinoOTA ready.");
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
  brightness = value;
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
  if (hasArduinoCloudCredentials()) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    ArduinoCloud.setBoardId(ARDUINO_CLOUD_DEVICE_ID);
    ArduinoCloud.setSecretDeviceKey(ARDUINO_CLOUD_DEVICE_KEY);
    ArduinoCloud.addProperty(powerOn, Permission::ReadWrite).onUpdate(onPowerOnChange);
    ArduinoCloud.addProperty(brightness, Permission::ReadWrite).onUpdate(onBrightnessChange);
    cloudEnabled = true;
    ArduinoCloud.begin(ArduinoIoTPreferredConnection);
    Serial.println("Arduino IoT Cloud connection started.");
    return;
  }

  if (strlen(WIFI_SSID) == 0) {
    Serial.println("Wi-Fi SSID is empty. Configure WIFI_SSID/WIFI_PASSWORD in include/config.h,");
    Serial.println("or create include/arduino_cloud_secrets.h for Arduino IoT Cloud.");
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
    startOTAIfNeeded();
  } else {
    Serial.println("Wi-Fi connection not established.");
  }
}

void maintainWiFi() {
  if (cloudEnabled || strlen(WIFI_SSID) == 0) return;
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
  ledcSetup(PWM_CHANNEL, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
  ledcAttachPin(PWM_PIN, PWM_CHANNEL);
  prefs.begin("cob-pwm", false);
  loadState();
  applyOutput();
  connectWiFi();
  startServer();
  Serial.println("COB PWM Controller ready.");
  Serial.printf("Brightness: %u%%, Power: %s\n", static_cast<unsigned int>(brightness), powerOn ? "ON" : "OFF");
}

void loop() {
  if (cloudEnabled) ArduinoCloud.update();
  server.handleClient();
  startOTAIfNeeded();
  if (WiFi.status() == WL_CONNECTED) ArduinoOTA.handle();
  else maintainWiFi();
}
