#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <math.h>

// -----------------------------------------------------------------------------
// ESP32 Wireless pH / ORP Monitor
// - First boot: starts setup Access Point
// - User configures Wi-Fi from browser
// - Normal mode: joins Wi-Fi and serves a live dashboard
// - Sensors: pH amplifier output and ORP amplifier output connected to ADC pins
// - Calibration: browser-based pH two-point calibration using known buffer liquids
// -----------------------------------------------------------------------------

static const int PH_ADC_PIN  = 34;   // ESP32 ADC input-only pin
static const int ORP_ADC_PIN = 35;   // ESP32 ADC input-only pin
static const int LED_PIN     = 2;

static const byte DNS_PORT = 53;
static const char* AP_SSID = "ESP32-PH-ORP-SETUP";
static const char* MDNS_NAME = "ph-orp-monitor";

WebServer server(80);
DNSServer dnsServer;
Preferences prefs;

struct DeviceConfig {
  String ssid;
  String password;

  // pH = phSlope * voltage + phOffset
  // These are calculated from two known buffer liquids, e.g. pH 7.00 and pH 4.00.
  float phSlope = -5.70f;
  float phOffset = 21.34f;

  // Stored pH calibration points for display/debugging.
  bool phCal1Set = false;
  bool phCal2Set = false;
  float phCal1Value = 7.00f;
  float phCal1Mv = 0.0f;
  float phCal2Value = 4.00f;
  float phCal2Mv = 0.0f;

  // ORP_mV = adc_mV * orpGain + orpOffset
  // Most ORP deployments use one known ORP solution to calculate offset.
  float orpGain = 1.0f;
  float orpOffset = 0.0f;
  bool orpCalSet = false;
  float orpCalKnownMv = 0.0f;
  float orpCalRawMv = 0.0f;

  uint16_t samples = 30;
};

DeviceConfig cfg;
bool apMode = false;
unsigned long lastBlink = 0;
bool ledState = false;

String htmlHeader(const String& title) {
  return "<!doctype html><html><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>" + title + "</title>"
         "<style>"
         "body{font-family:Arial,Helvetica,sans-serif;background:#f5f7fb;margin:0;padding:24px;color:#162033}"
         ".wrap{max-width:980px;margin:0 auto}.card{background:white;border-radius:14px;padding:20px;box-shadow:0 4px 18px rgba(0,0,0,.08);margin-bottom:18px}"
         "h1{margin:0 0 12px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:16px}.metric{font-size:38px;font-weight:700}.unit{font-size:16px;color:#667085}"
         "input,button{font-size:16px;padding:10px;border-radius:8px;border:1px solid #cfd6e4;width:100%;box-sizing:border-box}"
         "button{background:#1570ef;color:white;border:0;cursor:pointer;margin-top:12px}.secondary{background:#344054}.danger{background:#d92d20}.muted{color:#667085}.ok{color:#079455}.bad{color:#d92d20}"
         "label{font-weight:600;display:block;margin-top:12px}.row{display:grid;grid-template-columns:1fr 1fr;gap:12px}@media(max-width:600px){.row{grid-template-columns:1fr}}"
         "code{background:#eef2f6;padding:3px 6px;border-radius:6px}.nav a{margin-right:14px}.small{font-size:13px}.mono{font-family:monospace}"
         "</style></head><body><div class='wrap'>";
}

String htmlFooter() {
  return "</div></body></html>";
}

void loadConfig() {
  prefs.begin("phorp", true);
  cfg.ssid = prefs.getString("ssid", "");
  cfg.password = prefs.getString("pass", "");
  cfg.phSlope = prefs.getFloat("phSlope", cfg.phSlope);
  cfg.phOffset = prefs.getFloat("phOffset", cfg.phOffset);
  cfg.phCal1Set = prefs.getBool("phC1Set", cfg.phCal1Set);
  cfg.phCal2Set = prefs.getBool("phC2Set", cfg.phCal2Set);
  cfg.phCal1Value = prefs.getFloat("phC1Val", cfg.phCal1Value);
  cfg.phCal1Mv = prefs.getFloat("phC1Mv", cfg.phCal1Mv);
  cfg.phCal2Value = prefs.getFloat("phC2Val", cfg.phCal2Value);
  cfg.phCal2Mv = prefs.getFloat("phC2Mv", cfg.phCal2Mv);
  cfg.orpGain = prefs.getFloat("orpGain", cfg.orpGain);
  cfg.orpOffset = prefs.getFloat("orpOffset", cfg.orpOffset);
  cfg.orpCalSet = prefs.getBool("orpCSet", cfg.orpCalSet);
  cfg.orpCalKnownMv = prefs.getFloat("orpCKnown", cfg.orpCalKnownMv);
  cfg.orpCalRawMv = prefs.getFloat("orpCRaw", cfg.orpCalRawMv);
  cfg.samples = prefs.getUShort("samples", cfg.samples);
  prefs.end();
}

void saveConfig() {
  prefs.begin("phorp", false);
  prefs.putString("ssid", cfg.ssid);
  prefs.putString("pass", cfg.password);
  prefs.putFloat("phSlope", cfg.phSlope);
  prefs.putFloat("phOffset", cfg.phOffset);
  prefs.putBool("phC1Set", cfg.phCal1Set);
  prefs.putBool("phC2Set", cfg.phCal2Set);
  prefs.putFloat("phC1Val", cfg.phCal1Value);
  prefs.putFloat("phC1Mv", cfg.phCal1Mv);
  prefs.putFloat("phC2Val", cfg.phCal2Value);
  prefs.putFloat("phC2Mv", cfg.phCal2Mv);
  prefs.putFloat("orpGain", cfg.orpGain);
  prefs.putFloat("orpOffset", cfg.orpOffset);
  prefs.putBool("orpCSet", cfg.orpCalSet);
  prefs.putFloat("orpCKnown", cfg.orpCalKnownMv);
  prefs.putFloat("orpCRaw", cfg.orpCalRawMv);
  prefs.putUShort("samples", cfg.samples);
  prefs.end();
}

void clearWifiConfig() {
  prefs.begin("phorp", false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.end();
}

void clearCalibration() {
  cfg.phCal1Set = false;
  cfg.phCal2Set = false;
  cfg.phCal1Mv = 0.0f;
  cfg.phCal2Mv = 0.0f;
  cfg.phSlope = -5.70f;
  cfg.phOffset = 21.34f;
  cfg.orpCalSet = false;
  cfg.orpCalKnownMv = 0.0f;
  cfg.orpCalRawMv = 0.0f;
  cfg.orpGain = 1.0f;
  cfg.orpOffset = 0.0f;
  saveConfig();
}

float readMilliVoltsAveraged(int pin, uint16_t samples) {
  uint32_t total = 0;
  uint16_t n = max<uint16_t>(1, samples);
  for (uint16_t i = 0; i < n; i++) {
    total += analogReadMilliVolts(pin);
    delay(3);
  }
  return (float)total / (float)n;
}

struct Reading {
  float phMv;
  float orpMvRaw;
  float phVoltage;
  float ph;
  float orpMv;
};

Reading getReading() {
  Reading r;
  r.phMv = readMilliVoltsAveraged(PH_ADC_PIN, cfg.samples);
  r.orpMvRaw = readMilliVoltsAveraged(ORP_ADC_PIN, cfg.samples);
  r.phVoltage = r.phMv / 1000.0f;
  r.ph = cfg.phSlope * r.phVoltage + cfg.phOffset;
  r.orpMv = cfg.orpGain * r.orpMvRaw + cfg.orpOffset;
  return r;
}

String jsonEscape(const String& s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else out += c;
  }
  return out;
}

void redirectTo(const String& url) {
  server.sendHeader("Location", url, true);
  server.send(303, "text/plain", "");
}

String errorPage(const String& message) {
  return htmlHeader("Error") + "<div class='card'><h1>Calibration Error</h1><p class='bad'>" + message + "</p><p><a href='/calibrate'>Back to calibration</a></p></div>" + htmlFooter();
}

bool calculatePHCalibration() {
  float v1 = cfg.phCal1Mv / 1000.0f;
  float v2 = cfg.phCal2Mv / 1000.0f;
  if (fabs(v2 - v1) < 0.005f) {
    return false;
  }
  cfg.phSlope = (cfg.phCal2Value - cfg.phCal1Value) / (v2 - v1);
  cfg.phOffset = cfg.phCal1Value - (cfg.phSlope * v1);
  return true;
}

void handleApiReadings() {
  Reading r = getReading();
  String json = "{";
  json += "\"ph\":" + String(r.ph, 2) + ",";
  json += "\"ph_voltage\":" + String(r.phVoltage, 4) + ",";
  json += "\"ph_mv\":" + String(r.phMv, 1) + ",";
  json += "\"orp_mv\":" + String(r.orpMv, 1) + ",";
  json += "\"orp_adc_mv\":" + String(r.orpMvRaw, 1) + ",";
  json += "\"samples\":" + String(cfg.samples) + ",";
  json += "\"ph_slope\":" + String(cfg.phSlope, 6) + ",";
  json += "\"ph_offset\":" + String(cfg.phOffset, 6) + ",";
  json += "\"mode\":\"" + String(apMode ? "AP_SETUP" : "STA") + "\",";
  json += "\"ip\":\"" + (apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleApiStatus() {
  String json = "{";
  json += "\"ap_mode\":" + String(apMode ? "true" : "false") + ",";
  json += "\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"ip\":\"" + (apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "\",";
  json += "\"uptime_ms\":" + String(millis());
  json += "}";
  server.send(200, "application/json", json);
}

String nav() {
  return "<p class='nav'><a href='/'>Dashboard</a><a href='/calibrate'>Calibration</a><a href='/setup'>Settings</a><a href='/api/readings'>JSON API</a></p>";
}

String setupPage() {
  String s = htmlHeader("ESP32 pH/ORP Setup");
  s += "<div class='card'><h1>ESP32 pH / ORP Monitor Setup</h1>" + nav();
  s += "<p class='muted'>Configure Wi-Fi and basic ADC settings. Sensor calibration is available on the Calibration page.</p>";
  s += "<form method='POST' action='/save'>";
  s += "<label>Wi-Fi SSID</label><input name='ssid' value='" + cfg.ssid + "' required>";
  s += "<label>Wi-Fi Password</label><input name='password' type='password' value='" + cfg.password + "'>";
  s += "<label>ADC samples per reading</label><input name='samples' value='" + String(cfg.samples) + "'>";
  s += "<button type='submit'>Save and Reboot</button></form>";
  s += "<p class='muted small'>Current pH formula: <code>pH = " + String(cfg.phSlope, 4) + " * voltage + " + String(cfg.phOffset, 4) + "</code>.</p>";
  s += "</div>" + htmlFooter();
  return s;
}

String dashboardPage() {
  String s = htmlHeader("ESP32 pH/ORP Dashboard");
  s += "<div class='card'><h1>ESP32 pH / ORP Monitor</h1>" + nav();
  s += "<p class='muted'>Live dashboard for wireless pH and ORP readings.</p>";
  s += "<div class='grid'>";
  s += "<div class='card'><div class='muted'>pH</div><div id='ph' class='metric'>--</div><div id='phv' class='unit'>-- V</div></div>";
  s += "<div class='card'><div class='muted'>ORP</div><div id='orp' class='metric'>--</div><div class='unit'>mV</div></div>";
  s += "<div class='card'><div class='muted'>Status</div><div id='status' class='metric'>--</div><div id='ip' class='unit'>--</div></div>";
  s += "</div>";
  s += "<form method='POST' action='/resetwifi' onsubmit='return confirm(\"Clear Wi-Fi settings and reboot?\")'><button class='secondary'>Clear Wi-Fi and Restart Setup AP</button></form>";
  s += "</div>";
  s += "<script>async function tick(){try{let r=await fetch('/api/readings');let j=await r.json();";
  s += "document.getElementById('ph').innerText=Number(j.ph).toFixed(2);";
  s += "document.getElementById('phv').innerText=Number(j.ph_voltage).toFixed(4)+' V';";
  s += "document.getElementById('orp').innerText=Number(j.orp_mv).toFixed(1);";
  s += "document.getElementById('status').innerText=j.mode;";
  s += "document.getElementById('ip').innerText=j.ip;}catch(e){document.getElementById('status').innerText='Offline';}}";
  s += "tick();setInterval(tick,2000);</script>";
  s += htmlFooter();
  return s;
}

String calibrationPage() {
  Reading r = getReading();
  String s = htmlHeader("ESP32 pH/ORP Calibration");
  s += "<div class='card'><h1>Sensor Calibration</h1>" + nav();
  s += "<p class='muted'>Use known calibration liquids. For pH, place the probe in the first buffer, wait until the reading is stable, enter the known pH, then capture point 1. Rinse the probe, place it in the second buffer, enter the known pH, then capture point 2. The firmware calculates slope and offset automatically.</p>";
  s += "<div class='grid'>";
  s += "<div class='card'><div class='muted'>Current pH ADC</div><div class='metric'>" + String(r.phMv, 1) + "</div><div class='unit'>mV</div></div>";
  s += "<div class='card'><div class='muted'>Current pH</div><div class='metric'>" + String(r.ph, 2) + "</div><div class='unit'>calibrated value</div></div>";
  s += "<div class='card'><div class='muted'>Current ORP raw</div><div class='metric'>" + String(r.orpMvRaw, 1) + "</div><div class='unit'>ADC mV</div></div>";
  s += "</div></div>";

  s += "<div class='card'><h2>pH Two-Point Calibration</h2>";
  s += "<p class='muted'>Typical buffers are pH 7.00 and pH 4.00, or pH 7.00 and pH 10.00 depending on your measurement range.</p>";
  s += "<div class='row'>";
  s += "<form method='POST' action='/cal/ph1'><label>Point 1 known pH</label><input name='phKnown' value='7.00'><button>Capture pH Point 1</button></form>";
  s += "<form method='POST' action='/cal/ph2'><label>Point 2 known pH</label><input name='phKnown' value='4.00'><button>Capture pH Point 2 + Calculate</button></form>";
  s += "</div>";
  s += "<p class='small'>Point 1: " + String(cfg.phCal1Set ? "set" : "not set") + " — known pH " + String(cfg.phCal1Value, 2) + ", measured " + String(cfg.phCal1Mv, 1) + " mV</p>";
  s += "<p class='small'>Point 2: " + String(cfg.phCal2Set ? "set" : "not set") + " — known pH " + String(cfg.phCal2Value, 2) + ", measured " + String(cfg.phCal2Mv, 1) + " mV</p>";
  s += "<p class='small'>Formula: <code>pH = " + String(cfg.phSlope, 6) + " * voltage + " + String(cfg.phOffset, 6) + "</code></p>";
  s += "</div>";

  s += "<div class='card'><h2>ORP One-Point Offset Calibration</h2>";
  s += "<p class='muted'>Place the ORP probe in a known ORP calibration solution, enter its value in mV, then capture. This keeps gain at 1.0 and adjusts offset.</p>";
  s += "<form method='POST' action='/cal/orp'><label>Known ORP value, mV</label><input name='orpKnown' value='225'><button>Capture ORP Calibration</button></form>";
  s += "<p class='small'>ORP calibration: " + String(cfg.orpCalSet ? "set" : "not set") + " — known " + String(cfg.orpCalKnownMv, 1) + " mV, raw " + String(cfg.orpCalRawMv, 1) + " mV, offset " + String(cfg.orpOffset, 1) + " mV</p>";
  s += "</div>";

  s += "<div class='card'><h2>Reset Calibration</h2>";
  s += "<form method='POST' action='/cal/reset' onsubmit='return confirm(\"Reset pH and ORP calibration values?\")'><button class='danger'>Reset Calibration Defaults</button></form>";
  s += "</div>";
  s += htmlFooter();
  return s;
}

void handleRoot() {
  server.send(200, "text/html", apMode ? setupPage() : dashboardPage());
}

void handleSetup() {
  server.send(200, "text/html", setupPage());
}

void handleCalibration() {
  server.send(200, "text/html", calibrationPage());
}

void handleSave() {
  if (server.hasArg("ssid")) cfg.ssid = server.arg("ssid");
  if (server.hasArg("password")) cfg.password = server.arg("password");
  if (server.hasArg("samples")) cfg.samples = constrain(server.arg("samples").toInt(), 1, 500);
  saveConfig();
  server.send(200, "text/html", htmlHeader("Saved") + "<div class='card'><h1>Saved</h1><p>Device will reboot now.</p></div>" + htmlFooter());
  delay(1000);
  ESP.restart();
}

void handleCalPH1() {
  if (!server.hasArg("phKnown")) {
    server.send(400, "text/html", errorPage("Missing known pH value for point 1."));
    return;
  }
  cfg.phCal1Value = server.arg("phKnown").toFloat();
  cfg.phCal1Mv = readMilliVoltsAveraged(PH_ADC_PIN, max<uint16_t>(cfg.samples, 100));
  cfg.phCal1Set = true;
  saveConfig();
  Serial.printf("pH calibration point 1: %.2f pH at %.1f mV\n", cfg.phCal1Value, cfg.phCal1Mv);
  redirectTo("/calibrate");
}

void handleCalPH2() {
  if (!cfg.phCal1Set) {
    server.send(400, "text/html", errorPage("Capture pH point 1 first."));
    return;
  }
  if (!server.hasArg("phKnown")) {
    server.send(400, "text/html", errorPage("Missing known pH value for point 2."));
    return;
  }
  cfg.phCal2Value = server.arg("phKnown").toFloat();
  cfg.phCal2Mv = readMilliVoltsAveraged(PH_ADC_PIN, max<uint16_t>(cfg.samples, 100));
  cfg.phCal2Set = true;

  if (!calculatePHCalibration()) {
    saveConfig();
    server.send(400, "text/html", errorPage("The two pH calibration voltages are too close. Use two different buffer liquids and wait for stable readings."));
    return;
  }

  saveConfig();
  Serial.printf("pH calibration point 2: %.2f pH at %.1f mV\n", cfg.phCal2Value, cfg.phCal2Mv);
  Serial.printf("Calculated pH slope: %.6f, offset: %.6f\n", cfg.phSlope, cfg.phOffset);
  redirectTo("/calibrate");
}

void handleCalORP() {
  if (!server.hasArg("orpKnown")) {
    server.send(400, "text/html", errorPage("Missing known ORP value."));
    return;
  }
  cfg.orpCalKnownMv = server.arg("orpKnown").toFloat();
  cfg.orpCalRawMv = readMilliVoltsAveraged(ORP_ADC_PIN, max<uint16_t>(cfg.samples, 100));
  cfg.orpGain = 1.0f;
  cfg.orpOffset = cfg.orpCalKnownMv - cfg.orpCalRawMv;
  cfg.orpCalSet = true;
  saveConfig();
  Serial.printf("ORP calibration: known %.1f mV, raw %.1f mV, offset %.1f mV\n", cfg.orpCalKnownMv, cfg.orpCalRawMv, cfg.orpOffset);
  redirectTo("/calibrate");
}

void handleCalReset() {
  clearCalibration();
  redirectTo("/calibrate");
}

void handleResetWifi() {
  clearWifiConfig();
  server.send(200, "text/html", htmlHeader("Reset") + "<div class='card'><h1>Wi-Fi cleared</h1><p>Device will reboot into setup AP mode.</p></div>" + htmlFooter());
  delay(1000);
  ESP.restart();
}

void startWebRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/setup", HTTP_GET, handleSetup);
  server.on("/calibrate", HTTP_GET, handleCalibration);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/cal/ph1", HTTP_POST, handleCalPH1);
  server.on("/cal/ph2", HTTP_POST, handleCalPH2);
  server.on("/cal/orp", HTTP_POST, handleCalORP);
  server.on("/cal/reset", HTTP_POST, handleCalReset);
  server.on("/resetwifi", HTTP_POST, handleResetWifi);
  server.on("/api/readings", HTTP_GET, handleApiReadings);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.onNotFound([]() {
    if (apMode) server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    server.send(apMode ? 302 : 404, "text/plain", apMode ? "" : "Not found");
  });
  server.begin();
}

bool connectToWifi() {
  if (cfg.ssid.length() == 0) return false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.ssid.c_str(), cfg.password.c_str());
  Serial.printf("Connecting to Wi-Fi SSID: %s\n", cfg.ssid.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected. IP: ");
    Serial.println(WiFi.localIP());
    if (MDNS.begin(MDNS_NAME)) {
      Serial.printf("mDNS: http://%s.local\n", MDNS_NAME);
    }
    return true;
  }
  Serial.println("Wi-Fi connection failed. Starting setup AP.");
  return false;
}

void startSetupAP() {
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  IPAddress apIP = WiFi.softAPIP();
  dnsServer.start(DNS_PORT, "*", apIP);
  Serial.printf("Setup AP started: %s\n", AP_SSID);
  Serial.print("Open: http://");
  Serial.println(apIP);
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);
  delay(300);

  analogReadResolution(12);
  analogSetPinAttenuation(PH_ADC_PIN, ADC_11db);
  analogSetPinAttenuation(ORP_ADC_PIN, ADC_11db);

  loadConfig();

  if (!connectToWifi()) {
    startSetupAP();
  }

  startWebRoutes();
}

void loop() {
  if (apMode) dnsServer.processNextRequest();
  server.handleClient();

  // Simple heartbeat LED: slow in station mode, fast in AP/setup mode.
  unsigned long interval = apMode ? 250 : 1000;
  if (millis() - lastBlink >= interval) {
    lastBlink = millis();
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
  }
}
